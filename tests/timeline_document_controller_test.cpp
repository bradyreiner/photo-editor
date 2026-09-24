#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "job_runner.h"
#include "job_store.h"
#include "app_bootstrap.h"
#include "decoded_image_cache.h"
#include "timeline_asset_store.h"
#include "timeline_document_controller.h"

namespace {

using photo_editor::desktop::loadTimelineAssetRecords;
using photo_editor::desktop::loadPersistedJobRecords;
using photo_editor::desktop::removePersistedJobRecord;
using photo_editor::desktop::clear_decoded_image_cache;
using photo_editor::desktop::decoded_image_cache_budget_bytes;
using photo_editor::desktop::decoded_image_cache_snapshot;
using photo_editor::desktop::insert_decoded_image_cache_entry;
using photo_editor::desktop::set_decoded_image_cache_budget_bytes;
using photo_editor::desktop::TimelineDocumentController;
using photo_editor::desktop::AppBootstrap;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool nearly_equal(double left, double right, double tolerance = 0.0001) {
  return std::abs(left - right) <= tolerance;
}

bool wait_for_persisted_job(const QString& job_id, int timeout_ms = 120000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    for (const auto& job : loadPersistedJobRecords()) {
      if (job.job_id != job_id) {
        continue;
      }
      if (job.status == QStringLiteral("Completed") || job.status == QStringLiteral("Failed")) {
        if (job.status == QStringLiteral("Failed")) {
          std::cerr << "timeline render job failed: " << job.error_message.toStdString() << '\n';
          return false;
        }
        return true;
      }
    }
  }
  return false;
}

bool wait_for_waveform(TimelineDocumentController* controller, int timeout_ms = 10000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (controller->currentClip().value(QStringLiteral("waveformState")).toString() ==
        QStringLiteral("ready")) {
      return true;
    }
  }
  return false;
}

bool wait_for_program_scope(
    TimelineDocumentController* controller,
    int revision,
    int timeout_ms = 5000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller->programScopeAnalysisBusy() &&
        controller->programScopePublishedRevision() >= revision) {
      return true;
    }
  }
  return false;
}

bool wait_for_proxy(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto status = controller->currentClip().value(QStringLiteral("proxyStatus")).toString();
    if (status == QStringLiteral("ready")) {
      return true;
    }
    if (status == QStringLiteral("failed")) {
      return false;
    }
  }
  return false;
}

bool wait_for_source_integrity(
    TimelineDocumentController* controller,
    const QString& expected_status = QStringLiteral("verified"),
    int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (controller->currentClip()
            .value(QStringLiteral("sourceIntegrityStatus")).toString() ==
        expected_status) {
      return true;
    }
  }
  return false;
}

bool wait_for_readable_file(const QString& path, int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (QFileInfo(path).isReadable() && QFileInfo(path).size() > 0) {
      return true;
    }
  }
  return false;
}

bool wait_for_media_analysis(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->mediaAnalysisBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_silence_analysis(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->silenceAnalysisBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_transcription(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->transcriptionBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_beat_analysis(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->beatAnalysisBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_loudness_analysis(TimelineDocumentController* controller, int timeout_ms = 120000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->loudnessAnalysisBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_auto_reframe(TimelineDocumentController* controller, int timeout_ms = 30000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto status = controller->autoReframeStatus();
    if (!controller->autoReframeBusy() &&
        !status.startsWith(QStringLiteral("Finding")) &&
        !status.startsWith(QStringLiteral("Cancelling"))) {
      return true;
    }
  }
  return false;
}

bool wait_for_stabilization(
    TimelineDocumentController* controller,
    double* maximum_progress = nullptr,
    int timeout_ms = 120000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (maximum_progress != nullptr) {
      *maximum_progress = qMax(*maximum_progress, controller->stabilizationAnalysisProgress());
    }
    const auto state =
        controller->currentClip().value(QStringLiteral("stabilizationAnalysisState")).toString();
    if (!controller->stabilizationAnalysisBusy() && state != QStringLiteral("analyzing")) {
      return true;
    }
  }
  return false;
}

bool wait_for_render_cache(TimelineDocumentController* controller, int timeout_ms = 120000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->renderCacheBusy()) {
      return controller->renderCacheStatus() == QStringLiteral("ready");
    }
  }
  return false;
}

bool wait_for_compound(TimelineDocumentController* controller, int timeout_ms = 120000) {
  if (controller == nullptr) {
    return false;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto status =
        controller->currentClip().value(QStringLiteral("compoundStatus")).toString();
    if (status == QStringLiteral("ready")) {
      return true;
    }
    if (status == QStringLiteral("failed")) {
      std::cerr << "compound render failed: "
                << controller->currentClip()
                       .value(QStringLiteral("compoundError"))
                       .toString()
                       .toStdString()
                << '\n';
      return false;
    }
  }
  return false;
}

QString touch_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write("placeholder");
  file.close();
  return path;
}

bool run_process(const QString& program, const QStringList& arguments, QString* error_output = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_output != nullptr) {
      *error_output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return false;
  }
  return true;
}

QString process_stdout(const QString& program, const QStringList& arguments, QString* error_output = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_output != nullptr) {
      *error_output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return {};
  }

  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QByteArray process_bytes(const QString& program, const QStringList& arguments, QString* error_output = nullptr) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_output != nullptr) {
      *error_output = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return {};
  }
  return process.readAllStandardOutput();
}

QString process_stderr(const QString& program, const QStringList& arguments) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  return QString::fromUtf8(process.readAllStandardError());
}

double detected_mean_volume_db(const QString& ffmpeg, const QString& file_path, int frequency_hz) {
  const auto output = process_stderr(
      ffmpeg,
      {
          QStringLiteral("-v"), QStringLiteral("info"),
          QStringLiteral("-i"), file_path,
          QStringLiteral("-af"), QStringLiteral("bandpass=f=%1:w=40,volumedetect").arg(frequency_hz),
          QStringLiteral("-f"), QStringLiteral("null"),
          QStringLiteral("-"),
      });
  const auto marker = QStringLiteral("mean_volume:");
  const auto start = output.lastIndexOf(marker);
  if (start < 0) {
    return -120.0;
  }
  const auto value_start = start + marker.size();
  const auto value_end = output.indexOf(QStringLiteral(" dB"), value_start);
  bool ok = false;
  const auto value = output.mid(value_start, value_end - value_start).trimmed().toDouble(&ok);
  return ok ? value : -120.0;
}

double detected_channel_mean_volume_db(
    const QString& ffmpeg,
    const QString& file_path,
    int channel,
    double start_seconds = 0.0,
    double duration_seconds = 0.0) {
  QStringList arguments = {
      QStringLiteral("-v"), QStringLiteral("info"),
      QStringLiteral("-ss"), QString::number(qMax(0.0, start_seconds), 'f', 3),
      QStringLiteral("-i"), file_path,
  };
  if (duration_seconds > 0.0) {
    arguments << QStringLiteral("-t")
              << QString::number(duration_seconds, 'f', 3);
  }
  arguments << QStringLiteral("-af")
            << QStringLiteral("pan=mono|c0=c%1,volumedetect").arg(channel)
            << QStringLiteral("-f") << QStringLiteral("null")
            << QStringLiteral("-");
  const auto output = process_stderr(ffmpeg, arguments);
  const auto marker = QStringLiteral("mean_volume:");
  const auto start = output.lastIndexOf(marker);
  if (start < 0) {
    return -120.0;
  }
  const auto value_start = start + marker.size();
  const auto value_end = output.indexOf(QStringLiteral(" dB"), value_start);
  bool ok = false;
  const auto value = output.mid(value_start, value_end - value_start).trimmed().toDouble(&ok);
  return ok ? value : -120.0;
}

QString create_test_video(
    const QString& ffmpeg,
    const QString& output_path,
    const QString& color_name,
    bool with_audio,
    int audio_frequency_hz = 660) {
  QStringList arguments = {
      QStringLiteral("-y"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("color=c=%1:s=320x240:d=1.2:r=30").arg(color_name),
  };

  if (with_audio) {
    arguments << QStringLiteral("-f")
              << QStringLiteral("lavfi")
              << QStringLiteral("-i")
              << QStringLiteral("sine=frequency=%1:duration=1.2").arg(audio_frequency_hz);
  }

  arguments << QStringLiteral("-shortest")
            << QStringLiteral("-c:v")
            << QStringLiteral("libx264")
            << QStringLiteral("-pix_fmt")
            << QStringLiteral("yuv420p");

  if (with_audio) {
    arguments << QStringLiteral("-c:a")
              << QStringLiteral("aac");
  } else {
    arguments << QStringLiteral("-an");
  }

  arguments << output_path;

  QString error_output;
  if (!run_process(ffmpeg, arguments, &error_output)) {
    std::cerr << "ffmpeg fixture generation failed: " << error_output.toStdString() << '\n';
    return {};
  }

  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_test_video_with_channel_layout(
    const QString& ffmpeg,
    const QString& output_path,
    const QString& channel_layout) {
  const auto stereo = channel_layout == QStringLiteral("stereo");
  const auto audio_expression = stereo
      ? QStringLiteral(
            "aevalsrc=0.50*sin(2*PI*440*t)|0.05*sin(2*PI*880*t):s=48000:d=1.2:c=stereo")
      : QStringLiteral(
            "aevalsrc=0.18*sin(2*PI*220*t)|0.14*sin(2*PI*330*t)|"
            "0.10*sin(2*PI*440*t)|0.08*sin(2*PI*550*t)|"
            "0.06*sin(2*PI*660*t)|0.04*sin(2*PI*770*t):s=48000:d=1.2:c=5.1");
  QString error_output;
  if (!run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              QStringLiteral("color=c=navy:s=320x240:d=1.2:r=30"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              audio_expression,
              QStringLiteral("-shortest"), QStringLiteral("-c:v"),
              QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
              QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
              QStringLiteral("aac"), QStringLiteral("-ac"),
              stereo ? QStringLiteral("2") : QStringLiteral("6"), output_path,
          },
          &error_output)) {
    std::cerr << "channel-layout fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_test_timecode_video(
    const QString& ffmpeg,
    const QString& output_path,
    const QString& timecode,
    const QString& color_name) {
  QString error_output;
  if (!run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"),
              QStringLiteral("color=c=%1:s=320x240:d=1.2:r=30").arg(color_name),
              QStringLiteral("-timecode"), timecode,
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
              output_path,
          },
          &error_output)) {
    std::cerr << "ffmpeg timecode fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_test_video_with_silence(const QString& ffmpeg, const QString& output_path) {
  const QStringList arguments = {
      QStringLiteral("-y"),
      QStringLiteral("-v"),
      QStringLiteral("error"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("color=c=purple:s=320x240:d=2:r=30"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("aevalsrc=if(between(t\\,0.45\\,1.25)\\,0\\,sin(2*PI*440*t)):s=48000:d=2"),
      QStringLiteral("-shortest"),
      QStringLiteral("-c:v"),
      QStringLiteral("libx264"),
      QStringLiteral("-pix_fmt"),
      QStringLiteral("yuv420p"),
      QStringLiteral("-c:a"),
      QStringLiteral("aac"),
      output_path,
  };
  QString error_output;
  if (!run_process(ffmpeg, arguments, &error_output)) {
    std::cerr << "ffmpeg silence fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_test_video_with_beats(const QString& ffmpeg, const QString& output_path) {
  const QStringList arguments = {
      QStringLiteral("-y"),
      QStringLiteral("-v"),
      QStringLiteral("error"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("color=c=navy:s=320x240:d=2:r=30"),
      QStringLiteral("-f"),
      QStringLiteral("lavfi"),
      QStringLiteral("-i"),
      QStringLiteral("aevalsrc=if(between(t\\,0.20\\,0.24)+between(t\\,0.70\\,0.74)+between(t\\,1.20\\,1.24)+between(t\\,1.70\\,1.74)\\,0.9*sin(2*PI*900*t)\\,0):s=48000:d=2"),
      QStringLiteral("-shortest"),
      QStringLiteral("-c:v"),
      QStringLiteral("libx264"),
      QStringLiteral("-pix_fmt"),
      QStringLiteral("yuv420p"),
      QStringLiteral("-c:a"),
      QStringLiteral("aac"),
      output_path,
  };
  QString error_output;
  if (!run_process(ffmpeg, arguments, &error_output)) {
    std::cerr << "ffmpeg beat fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_moving_subject_video(const QString& ffmpeg, const QString& output_path) {
  QString error_output;
  if (!run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("color=c=black:s=640x360:d=2:r=30"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("color=c=white:s=120x200:d=2:r=30"),
              QStringLiteral("-filter_complex"),
              QStringLiteral("[0:v][1:v]overlay=x='160+100*t':y=80:shortest=1"),
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
              output_path,
          },
          &error_output)) {
    std::cerr << "moving-subject fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

QString create_shaky_camera_video(const QString& ffmpeg, const QString& output_path) {
  QString error_output;
  if (!run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              QStringLiteral(
                  "testsrc2=size=400x300:rate=30:duration=3,"
                  "crop=320:240:x='40+18*sin(6*PI*t)':y='30+12*cos(4*PI*t)'"),
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), output_path,
          },
          &error_output)) {
    std::cerr << "shaky-camera fixture generation failed: "
              << error_output.toStdString() << '\n';
    return {};
  }
  return QFileInfo::exists(output_path) ? output_path : QString{};
}

double bright_subject_center_x(
    const QString& ffmpeg,
    const QString& file_path,
    double seconds,
    int width,
    int height) {
  const auto frame = process_bytes(
      ffmpeg,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-ss"), QString::number(seconds, 'f', 3),
       QStringLiteral("-i"), file_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-vf"), QStringLiteral("format=gray"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
  if (frame.size() < width * height) {
    return -1.0;
  }
  auto weighted_x = 0.0;
  auto total_weight = 0.0;
  for (auto y = 0; y < height; ++y) {
    for (auto x = 0; x < width; ++x) {
      const auto luminance = static_cast<unsigned char>(frame.at(y * width + x));
      if (luminance < 180) {
        continue;
      }
      weighted_x += (static_cast<double>(x) + 0.5) * luminance;
      total_weight += luminance;
    }
  }
  return total_weight > 0.0 ? weighted_x / total_weight / width : -1.0;
}

QString create_rotation_test_image(const QString& output_path) {
  QImage image(80, 40, QImage::Format_RGB32);
  for (auto y = 0; y < image.height(); ++y) {
    for (auto x = 0; x < image.width(); ++x) {
      image.setPixel(x, y, x < image.width() / 2 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
    }
  }
  return image.save(output_path) ? output_path : QString{};
}

QString create_position_test_image(const QString& output_path) {
  QImage image(320, 240, QImage::Format_RGB32);
  image.fill(qRgb(0, 0, 0));
  for (auto y = 100; y < 140; ++y) {
    for (auto x = 140; x < 180; ++x) {
      image.setPixel(x, y, qRgb(255, 255, 255));
    }
  }
  return image.save(output_path) ? output_path : QString{};
}

QString create_alpha_delivery_test_image(const QString& output_path) {
  QImage image(80, 80, QImage::Format_RGBA8888);
  image.fill(Qt::transparent);
  for (auto y = 20; y < 60; ++y) {
    for (auto x = 20; x < 60; ++x) {
      image.setPixelColor(x, y, QColor(255, 72, 48, 255));
    }
  }
  return image.save(output_path) ? output_path : QString{};
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }

  qputenv("HOME", workspace.path().toUtf8());
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv(
      "PHOTO_EDITOR_TIMELINE_CAPTION_TEMPLATE_PATH",
      workspace.filePath(QStringLiteral("caption-style-templates.json")).toUtf8());

  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);

  const auto first_video = touch_file(workspace.filePath(QStringLiteral("clip-01.mp4")));
  const auto second_video = touch_file(workspace.filePath(QStringLiteral("clip-02.mov")));
  const auto photo_still = touch_file(workspace.filePath(QStringLiteral("still-01.jpg")));
  const auto audio_clip = touch_file(workspace.filePath(QStringLiteral("audio-01.wav")));
  if (first_video.isEmpty() || second_video.isEmpty()) {
    return fail("video placeholders should be created");
  }

  TimelineDocumentController import_failure_controller;
  import_failure_controller.createTimeline(QStringLiteral("Import Failures"));
  import_failure_controller.addClipFromGallery(
      QStringLiteral("video:missing"),
      workspace.filePath(QStringLiteral("missing.mp4")),
      QStringLiteral("Missing"),
      2.0,
      QString{});
  const auto unsupported_file = touch_file(workspace.filePath(QStringLiteral("unsupported.txt")));
  import_failure_controller.addClipFromGallery(
      QStringLiteral("other:unsupported"), unsupported_file, QStringLiteral("Unsupported"), 2.0, QString{});
  if (import_failure_controller.clipCount() != 0 ||
      !import_failure_controller.status().contains(QStringLiteral("Unable to read")) ||
      import_failure_controller.canUndo()) {
    return fail("missing and unsupported media should fail without creating corrupt timeline clips");
  }
  const auto offline_file = touch_file(workspace.filePath(QStringLiteral("offline.mp4")));
  import_failure_controller.addClipFromGallery(
      QStringLiteral("video:offline"), offline_file, QStringLiteral("Offline Fixture"), 2.0, QStringLiteral("poster://offline"));
  if (import_failure_controller.clipCount() != 1 || !QFile::remove(offline_file) ||
      import_failure_controller.currentClip().value(QStringLiteral("mediaOnline")).toBool()) {
    return fail("clips whose source disappears should remain visibly identifiable as offline media");
  }

  TimelineDocumentController controller;
  if (controller.hasActiveTimeline()) {
    return fail("timeline controller should start without an implicit active timeline");
  }
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Gallery Timeline")},
      {QStringLiteral("width"), 1920},
      {QStringLiteral("height"), 1080},
      {QStringLiteral("frameRate"), 24.0},
      {QStringLiteral("audioSampleRate"), 44100},
      {QStringLiteral("projectBinding"), QStringLiteral("Campaign")},
      {QStringLiteral("colorSpace"), QStringLiteral("Display P3")},
      {QStringLiteral("dataLevels"), QStringLiteral("Full")},
      {QStringLiteral("exportContainer"), QStringLiteral("mov")},
      {QStringLiteral("exportCodec"), QStringLiteral("prores")},
      {QStringLiteral("exportPreset"), QStringLiteral("TikTok")},
  });
  if (!controller.hasActiveTimeline() ||
      controller.sequenceWidth() != 1920 ||
      controller.sequenceHeight() != 1080 ||
      !nearly_equal(controller.framesPerSecond(), 24.0) ||
      controller.audioSampleRate() != 44100 ||
      controller.projectBinding() != QStringLiteral("Campaign") ||
      controller.colorSpace() != QStringLiteral("Display P3") ||
      controller.dataLevels() != QStringLiteral("Full") ||
      controller.exportContainer() != QStringLiteral("mov") ||
      controller.exportCodec() != QStringLiteral("prores") ||
      controller.exportPreset() != QStringLiteral("tiktok")) {
    return fail("explicit timeline setup should persist professional sequence settings");
  }
  controller.addClipFromGallery(QStringLiteral("video:1"), first_video, QStringLiteral("Clip One"), 12.0, QStringLiteral("poster://one"));
  controller.addClipFromGallery(QStringLiteral("video:2"), second_video, QStringLiteral("Clip Two"), 8.0, QStringLiteral("poster://two"));

  if (controller.sequenceWidth() != 1920 || controller.sequenceHeight() != 1080) {
    return fail("adding media should not replace the editor's explicit sequence dimensions");
  }
  if (controller.clipCount() != 2) {
    return fail("adding gallery videos should append clips");
  }
  if (controller.trackCount() != 2 || controller.videoTrackCount() != 1 || controller.audioTrackCount() != 1) {
    return fail("timeline documents should expose dedicated video and audio tracks");
  }

  TimelineDocumentController version_controller;
  const auto version_reference_still = create_position_test_image(
      workspace.filePath(QStringLiteral("version-reference.png")));
  if (version_reference_still.isEmpty()) {
    return fail("the version comparison reference fixture should be a readable image");
  }
  version_controller.createTimeline(QStringLiteral("Versioned Cut"));
  version_controller.addClipFromGallery(
      QStringLiteral("photo:version-a"),
      version_reference_still,
      QStringLiteral("Opening"),
      4.0,
      QString{});
  const auto version_marker_id =
      version_controller.addSequenceMarker(QStringLiteral("Review opening"));
  if (!version_controller.createSequenceVersionWithMetadata(
          QStringLiteral("Client review"),
          QStringLiteral("Opening approved; preserve this handoff."),
          QStringLiteral("Avery Editor"),
          true)) {
    return fail("a named sequence version should be created without manual file duplication");
  }
  const auto initial_versions = version_controller.sequenceVersions();
  if (initial_versions.size() != 1) {
    return fail("a newly created named version should be listed exactly once");
  }
  const auto comparison_current_path =
      workspace.filePath(QStringLiteral("comparison-current.png"));
  const auto comparison_reference_path =
      workspace.filePath(QStringLiteral("comparison-reference.png"));
  QImage comparison_current(64, 48, QImage::Format_RGBA8888);
  QImage comparison_reference(64, 48, QImage::Format_RGBA8888);
  comparison_current.fill(QColor(0, 0, 0, 255));
  comparison_reference.fill(QColor(255, 64, 32, 255));
  if (!comparison_current.save(comparison_current_path) ||
      !comparison_reference.save(comparison_reference_path)) {
    return fail("the Program difference fixtures should be writable");
  }
  const auto comparison_capture_path =
      version_controller.programComparisonCapturePath(QStringLiteral("current"), 7);
  if (comparison_capture_path.isEmpty() ||
      !QFileInfo(comparison_capture_path).absoluteDir().exists() ||
      !comparison_capture_path.endsWith(
          QStringLiteral("timeline-program-comparison-current-7.png"))) {
    return fail("Program comparison should prepare a writable bounded capture location");
  }
  const auto difference_result = version_controller.renderProgramComparisonDifference(
      comparison_current_path, comparison_reference_path, 1.0);
  const QImage difference_image(
      difference_result.value(QStringLiteral("path")).toString());
  const auto difference_pixel = difference_image.pixelColor(32, 24);
  if (!difference_result.value(QStringLiteral("success")).toBool() ||
      difference_result.value(QStringLiteral("space")).toString() !=
          QStringLiteral("linear-light-srgb") ||
      difference_image.size() != comparison_current.size() ||
      difference_pixel.red() < 245 || difference_pixel.green() < 55 ||
      difference_pixel.blue() < 25 || difference_pixel.alpha() != 255) {
    return fail("Program difference should compute a visible opaque absolute difference in linear light");
  }
  version_controller.requestProgramScopeAnalysis(comparison_reference_path, 11);
  if (!wait_for_program_scope(&version_controller, 11)) {
    return fail("Program scope analysis should complete asynchronously without blocking the controller");
  }
  const auto scope_data = version_controller.programScopeData();
  if (!scope_data.value(QStringLiteral("success")).toBool() ||
      !scope_data.value(QStringLiteral("bounded")).toBool() ||
      !scope_data.value(QStringLiteral("workerThread")).toBool() ||
      scope_data.value(QStringLiteral("histogramLuma")).toList().size() != 256 ||
      scope_data.value(QStringLiteral("waveformLuma")).toList().size() != 96 ||
      scope_data.value(QStringLiteral("densityLuma")).toList().size() != 96 * 64 ||
      scope_data.value(QStringLiteral("vectorscopePoints")).toList().isEmpty() ||
      scope_data.value(QStringLiteral("sampleCount")).toInt() <= 0) {
    return fail("Program scopes should publish bounded waveform, vectorscope, parade, and histogram data from the captured frame");
  }
  version_controller.requestProgramScopeAnalysis(comparison_current_path, 12);
  version_controller.requestProgramScopeAnalysis(comparison_reference_path, 13);
  if (!wait_for_program_scope(&version_controller, 13) ||
      version_controller.programScopeData().value(QStringLiteral("revision")).toInt() != 13) {
    return fail("Program scope analysis should publish the latest queued revision and reject stale completion");
  }
  version_controller.clearProgramScopeAnalysis();
  if (!version_controller.programScopeData().isEmpty() ||
      version_controller.programScopePublishedRevision() != -1) {
    return fail("closing Program scopes should release their derived analysis state");
  }
  const auto named_version = initial_versions.first().toMap();
  const auto named_version_id = named_version.value(QStringLiteral("versionId")).toString();
  const auto named_version_path = named_version.value(QStringLiteral("documentPath")).toString();
  const auto named_comparison_media_path =
      named_version.value(QStringLiteral("comparisonMediaPath")).toString();
  if (named_version.value(QStringLiteral("label")).toString() != QStringLiteral("Client review") ||
      named_version.value(QStringLiteral("kind")).toString() != QStringLiteral("named") ||
      named_version.value(QStringLiteral("notes")).toString() !=
          QStringLiteral("Opening approved; preserve this handoff.") ||
      named_version.value(QStringLiteral("author")).toString() !=
          QStringLiteral("Avery Editor") ||
      !named_version.value(QStringLiteral("protected")).toBool() ||
      named_version.value(QStringLiteral("createdAt")).toString().isEmpty() ||
      !named_version.value(QStringLiteral("matchesCurrent")).toBool() ||
      named_version.value(QStringLiteral("comparisonSummary")).toString() !=
          QStringLiteral("Matches the current edit") ||
      named_version.value(QStringLiteral("comparisonMediaKind")).toString() !=
          QStringLiteral("image") ||
      !named_version.value(QStringLiteral("comparisonMediaAvailable")).toBool() ||
      named_version.value(QStringLiteral("comparisonMediaUrl")).toUrl().toLocalFile() !=
          named_comparison_media_path ||
      named_comparison_media_path == version_reference_still ||
      !QFileInfo(named_comparison_media_path).isReadable() ||
      named_version_id.isEmpty() || !QFileInfo::exists(named_version_path)) {
    return fail("named versions should expose durable identity, metadata, comparison state, and an immutable visual reference");
  }

  QFile original_milestone_file(named_version_path);
  if (!original_milestone_file.open(QIODevice::ReadOnly)) {
    return fail("the protected milestone fixture should read its immutable snapshot");
  }
  const auto original_milestone_document =
      QJsonDocument::fromJson(original_milestone_file.readAll()).object();
  original_milestone_file.close();
  if (version_controller.deleteSequenceVersion(named_version_id) ||
      !QFileInfo::exists(named_version_path)) {
    return fail("protected milestones should reject deletion until explicitly unprotected");
  }
  if (!version_controller.updateSequenceVersionMetadata(
          named_version_id,
          QStringLiteral("Client review"),
          QStringLiteral("Opening approved; preserve this handoff."),
          QStringLiteral("Avery Editor"),
          false) ||
      version_controller.sequenceVersionComparison(named_version_id)
          .value(QStringLiteral("protected")).toBool()) {
    return fail("milestone protection should require and honor an explicit metadata action");
  }
  if (!version_controller.updateSequenceVersionMetadata(
          named_version_id,
          QStringLiteral("Picture Lock"),
          QStringLiteral("Approved for sound and color handoff."),
          QStringLiteral("Avery Editor"),
          true)) {
    return fail("named version metadata should remain editable without replacing its snapshot");
  }
  QFile updated_milestone_file(named_version_path);
  if (!updated_milestone_file.open(QIODevice::ReadOnly)) {
    return fail("updated milestone metadata should remain readable");
  }
  const auto updated_milestone_document =
      QJsonDocument::fromJson(updated_milestone_file.readAll()).object();
  updated_milestone_file.close();
  const auto updated_milestone_metadata =
      updated_milestone_document.value(QStringLiteral("_sequenceVersion")).toObject();
  if (updated_milestone_metadata.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Picture Lock") ||
      updated_milestone_metadata.value(QStringLiteral("notes")).toString() !=
          QStringLiteral("Approved for sound and color handoff.") ||
      updated_milestone_metadata.value(QStringLiteral("metadataUpdatedAt")).toString().isEmpty() ||
      updated_milestone_document.value(QStringLiteral("tracks")) !=
          original_milestone_document.value(QStringLiteral("tracks")) ||
      updated_milestone_document.value(QStringLiteral("markers")) !=
          original_milestone_document.value(QStringLiteral("markers"))) {
    return fail("editing milestone details must not mutate immutable sequence content");
  }
  if (!version_controller.duplicateSequenceVersion(
          named_version_id, QStringLiteral("Picture Lock Branch"))) {
    return fail("an immutable snapshot should duplicate without restoring it first");
  }
  QVariantMap duplicated_version;
  for (const auto& value : version_controller.sequenceVersions()) {
    const auto entry = value.toMap();
    if (entry.value(QStringLiteral("duplicatedFromVersionId")).toString() ==
        named_version_id) {
      duplicated_version = entry;
      break;
    }
  }
  const auto duplicated_version_path =
      duplicated_version.value(QStringLiteral("documentPath")).toString();
  const auto duplicated_comparison_media_path =
      duplicated_version.value(QStringLiteral("comparisonMediaPath")).toString();
  QFile duplicated_version_file(duplicated_version_path);
  if (duplicated_version.isEmpty() ||
      duplicated_version.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Picture Lock Branch") ||
      duplicated_version.value(QStringLiteral("protected")).toBool() ||
      duplicated_comparison_media_path.isEmpty() ||
      duplicated_comparison_media_path == named_comparison_media_path ||
      !QFileInfo(duplicated_comparison_media_path).isReadable() ||
      !duplicated_version_file.open(QIODevice::ReadOnly)) {
    return fail("duplicating should create a new, unprotected named identity linked to its source");
  }
  const auto duplicated_version_document =
      QJsonDocument::fromJson(duplicated_version_file.readAll()).object();
  duplicated_version_file.close();
  if (duplicated_version_document.value(QStringLiteral("tracks")) !=
          updated_milestone_document.value(QStringLiteral("tracks")) ||
      !version_controller.deleteSequenceVersion(
          duplicated_version.value(QStringLiteral("versionId")).toString()) ||
      QFileInfo::exists(duplicated_version_path) ||
      QFileInfo::exists(duplicated_comparison_media_path)) {
    return fail("a duplicated version should preserve exact snapshot content and comparison media, then remain independently removable");
  }

  version_controller.addClipFromGallery(
      QStringLiteral("photo:version-b"),
      version_reference_still,
      QStringLiteral("Closing"),
      3.0,
      QString{});
  version_controller.selectClip(0);
  version_controller.setSelectedClipOpacity(0.5);
  version_controller.setSequenceMarkerNote(
      version_marker_id, QStringLiteral("Changed after picture lock"));
  version_controller.applyOutputPreset(QStringLiteral("tiktok"));
  const auto changed_comparison = version_controller.sequenceVersionComparison(named_version_id);
  if (changed_comparison.value(QStringLiteral("clipDelta")).toInt() != -1 ||
      changed_comparison.value(QStringLiteral("durationDeltaSeconds")).toDouble() >= -2.9 ||
      !changed_comparison.value(QStringLiteral("addedClipLabels")).toStringList().contains(
          QStringLiteral("Closing")) ||
      !changed_comparison.value(QStringLiteral("changedClipLabels")).toStringList().contains(
          QStringLiteral("Opening")) ||
      !changed_comparison.value(QStringLiteral("changedMarkerLabels")).toStringList().contains(
          QStringLiteral("Review opening")) ||
      !changed_comparison.value(QStringLiteral("formatChanged")).toBool() ||
      !changed_comparison.value(QStringLiteral("finishingChanged")).toBool() ||
      changed_comparison.value(QStringLiteral("matchesCurrent")).toBool() ||
      !changed_comparison.value(QStringLiteral("comparisonSummary")).toString().contains(
          QStringLiteral("added"))) {
    return fail("version comparison should identify exact clip, marker, format, and finishing changes, not only count deltas");
  }
  QFile immutable_version_file(named_version_path);
  if (!immutable_version_file.open(QIODevice::ReadOnly) ||
      QJsonDocument::fromJson(immutable_version_file.readAll())
              .object()
              .value(QStringLiteral("_sequenceVersion"))
              .toObject()
              .value(QStringLiteral("clipCount"))
              .toInt() != 1) {
    return fail("named version documents should remain immutable after later sequence edits");
  }

  if (!version_controller.restoreSequenceVersion(named_version_id) ||
      version_controller.clipCount() != 1 ||
      version_controller.sequenceMarkers().size() != 1 ||
      !version_controller.dirty() ||
      !version_controller.canUndo()) {
    return fail("restoring a named version should atomically replace edit state and remain undoable");
  }
  auto found_restore_checkpoint = false;
  for (const auto& value : version_controller.sequenceVersions()) {
    const auto entry = value.toMap();
    if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("recovery") &&
        entry.value(QStringLiteral("label")).toString().contains(QStringLiteral("Before restoring"))) {
      found_restore_checkpoint = true;
      break;
    }
  }
  if (!found_restore_checkpoint) {
    return fail("every version restore should first create a visible recovery checkpoint");
  }
  version_controller.undo();
  if (version_controller.clipCount() != 2) {
    return fail("undo should return to the edit that existed immediately before a version restore");
  }
  version_controller.redo();
  if (version_controller.clipCount() != 1) {
    return fail("redo should atomically reapply a restored sequence version");
  }

  TimelineDocumentController reopened_version_controller;
  if (!reopened_version_controller.loadTimeline(named_version_path) ||
      reopened_version_controller.sequenceVersions().size() < 2 ||
      reopened_version_controller.sequenceVersionComparison(named_version_id).isEmpty()) {
    return fail("timeline-scoped named and recovery versions should remain discoverable after reopening");
  }
  const auto reopened_milestone =
      reopened_version_controller.sequenceVersionComparison(named_version_id);
  if (reopened_milestone.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Picture Lock") ||
      reopened_milestone.value(QStringLiteral("notes")).toString() !=
          QStringLiteral("Approved for sound and color handoff.") ||
      reopened_milestone.value(QStringLiteral("author")).toString() !=
          QStringLiteral("Avery Editor") ||
      !reopened_milestone.value(QStringLiteral("protected")).toBool()) {
    return fail("named milestone metadata and protection should remain timeline-scoped after reopen");
  }
  const auto autosave_path = reopened_version_controller.savedDocumentPath() +
                             QStringLiteral(".autosave");
  QFile autosave_source(named_version_path);
  if (!autosave_source.open(QIODevice::ReadOnly)) {
    return fail("the autosave recovery fixture should read the named version document");
  }
  auto autosave_object = QJsonDocument::fromJson(autosave_source.readAll()).object();
  autosave_object.remove(QStringLiteral("_sequenceVersion"));
  QFile autosave_file(autosave_path);
  if (!autosave_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      autosave_file.write(QJsonDocument(autosave_object).toJson(QJsonDocument::Indented)) <= 0) {
    return fail("the autosave recovery fixture should write a plain timeline sidecar");
  }
  autosave_file.close();
  if (!autosave_file.open(QIODevice::ReadWrite) ||
      !autosave_file.setFileTime(
          QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime)) {
    return fail("the autosave recovery fixture should be newer than the saved document");
  }
  autosave_file.close();
  const auto autosave_comparison = reopened_version_controller.sequenceVersionComparison(
      QStringLiteral("autosave"));
  if (autosave_comparison.value(QStringLiteral("kind")).toString() != QStringLiteral("autosave") ||
      autosave_comparison.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Autosave recovery") ||
      !autosave_comparison.value(QStringLiteral("isRecovery")).toBool() ||
      !reopened_version_controller.restoreSequenceVersion(QStringLiteral("autosave"))) {
    return fail("a newer autosave should be visible and restorable from the same Versions workflow");
  }

  TimelineDocumentController journal_controller;
  journal_controller.createTimeline(QStringLiteral("Crash-safe Cut"));
  journal_controller.addClipFromGallery(
      QStringLiteral("photo:journal"),
      photo_still,
      QStringLiteral("Journal Opening"),
      4.0,
      QString{});
  if (!journal_controller.saveTimeline()) {
    return fail("the edit-journal fixture should establish a last known-good saved project");
  }
  const auto journal_saved_path = journal_controller.savedDocumentPath();
  const auto journal_path = journal_controller.editJournalPath();
  QFile saved_before_recovery_file(journal_saved_path);
  if (!saved_before_recovery_file.open(QIODevice::ReadOnly)) {
    return fail("the edit-journal fixture should read the canonical saved project");
  }
  const auto saved_before_recovery = saved_before_recovery_file.readAll();
  saved_before_recovery_file.close();

  journal_controller.addSequenceMarker(QStringLiteral("Unsaved crash-safe note"));
  if (!journal_controller.flushEditJournal()) {
    return fail("committed timeline edits should flush to an append-only journal on demand");
  }

  QFile journal_file(journal_path);
  if (!journal_file.open(QIODevice::ReadOnly)) {
    return fail("the timeline journal should be durable outside the canonical project document");
  }
  auto previous_entry_hash = QString{};
  auto verified_journal_entries = 0;
  while (!journal_file.atEnd()) {
    const auto line = journal_file.readLine().trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const auto entry_document = QJsonDocument::fromJson(line);
    if (!entry_document.isObject()) {
      return fail("every complete journal line should be standalone valid JSON");
    }
    auto entry = entry_document.object();
    auto timeline_document = entry.value(QStringLiteral("document")).toObject();
    timeline_document.remove(QStringLiteral("updatedAt"));
    const auto expected_document_hash = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(timeline_document).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
    const auto stored_entry_hash = entry.take(QStringLiteral("entryHash")).toString();
    const auto expected_entry_hash = QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(entry).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
    if (entry.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("timeline-edit-journal:v1") ||
        entry.value(QStringLiteral("previousEntryHash")).toString() != previous_entry_hash ||
        entry.value(QStringLiteral("documentHash")).toString() != expected_document_hash ||
        stored_entry_hash != expected_entry_hash) {
      return fail("journal entries should form a deterministic SHA-256 chain over full edit snapshots");
    }
    previous_entry_hash = stored_entry_hash;
    ++verified_journal_entries;
  }
  journal_file.close();
  if (verified_journal_entries < 2) {
    return fail("save and subsequent edit states should append instead of replacing journal history");
  }

  if (!journal_file.open(QIODevice::WriteOnly | QIODevice::Append) ||
      journal_file.write("{\"schema\":\"timeline-edit-journal:v1\"") <= 0) {
    return fail("the crash fixture should append an intentionally truncated final journal record");
  }
  journal_file.close();

  TimelineDocumentController recovery_controller;
  recovery_controller.refreshRecoveryCandidates();
  QVariantMap journal_candidate;
  for (const auto& value : recovery_controller.recoveryCandidates()) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("sourcePath")).toString() == journal_path) {
      journal_candidate = candidate;
      break;
    }
  }
  if (journal_candidate.isEmpty() ||
      journal_candidate.value(QStringLiteral("kind")).toString() != QStringLiteral("journal") ||
      !journal_candidate.value(QStringLiteral("damagedTail")).toBool() ||
      journal_candidate.value(QStringLiteral("validEntryCount")).toInt() != verified_journal_entries ||
      !journal_candidate.value(QStringLiteral("openExplanation")).toString().contains(
          QStringLiteral("not changed until you save")) ||
      !journal_candidate.value(QStringLiteral("restoreCopyExplanation")).toString().contains(
          QStringLiteral("original project remains untouched"))) {
    return fail("recovery discovery should explain and expose the latest valid entry before a damaged tail");
  }
  const auto journal_candidate_id =
      journal_candidate.value(QStringLiteral("candidateId")).toString();
  if (!recovery_controller.openRecoveryCandidate(journal_candidate_id) ||
      !recovery_controller.dirty() || recovery_controller.sequenceMarkers().size() != 1 ||
      recovery_controller.sequenceMarkers().first().toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Unsaved crash-safe note")) {
    return fail("Open recovery should materialize the latest verified edit as a dirty working timeline");
  }
  QFile saved_after_open_file(journal_saved_path);
  if (!saved_after_open_file.open(QIODevice::ReadOnly) ||
      saved_after_open_file.readAll() != saved_before_recovery) {
    return fail("opening recovery must not mutate the last known-good saved project");
  }
  saved_after_open_file.close();

  QFile damaged_saved_file(journal_saved_path);
  if (!damaged_saved_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      damaged_saved_file.write("{damaged-project") <= 0) {
    return fail("the recovery fixture should be able to simulate a damaged canonical project");
  }
  damaged_saved_file.close();
  TimelineDocumentController copy_recovery_controller;
  if (!copy_recovery_controller.restoreRecoveryCandidateAsCopy(journal_candidate_id) ||
      copy_recovery_controller.savedDocumentPath() == journal_saved_path ||
      !QFileInfo::exists(copy_recovery_controller.savedDocumentPath()) ||
      !copy_recovery_controller.timelineName().contains(QStringLiteral("Recovered Copy")) ||
      copy_recovery_controller.sequenceMarkers().size() != 1) {
    return fail("Restore Copy should create a durable independent timeline from a journal even when the original is damaged");
  }
  if (!copy_recovery_controller.discardRecoveryCandidate(journal_candidate_id)) {
    return fail("Discard should archive a journal recovery without touching either project document");
  }
  for (const auto& value : copy_recovery_controller.recoveryCandidates()) {
    if (value.toMap().value(QStringLiteral("candidateId")).toString() == journal_candidate_id) {
      return fail("a discarded recovery should no longer be offered at startup");
    }
  }

  controller.selectClip(0);
  controller.setSelectedTrimStartSeconds(2.0);
  controller.setSelectedTrimEndSeconds(9.5);
  const auto first_clip = controller.currentClip();
  if (first_clip.value(QStringLiteral("trimStartSeconds")).toDouble() != 2.0 ||
      first_clip.value(QStringLiteral("trimEndSeconds")).toDouble() != 9.5) {
    return fail("trim updates should persist on the selected clip");
  }

  controller.trimSelectedClipStartToSequenceSeconds(1.0);
  if (!nearly_equal(controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 1.0) ||
      !nearly_equal(controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 3.0)) {
    return fail("timeline start-edge trims should move the clip start while updating trim in");
  }

  controller.trimSelectedClipEndToSequenceSeconds(6.5);
  if (!nearly_equal(controller.currentClip().value(QStringLiteral("trimEndSeconds")).toDouble(), 8.5) ||
      !nearly_equal(controller.currentClip().value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 5.5)) {
    return fail("timeline end-edge trims should update the clip out point from sequence time");
  }

  TimelineDocumentController keyboard_trim_controller;
  keyboard_trim_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Keyboard Trim")},
      {QStringLiteral("width"), 1920},
      {QStringLiteral("height"), 1080},
      {QStringLiteral("frameRate"), 25.0},
  });
  keyboard_trim_controller.addClipFromGallery(
      QStringLiteral("video:keyboard-trim"), first_video,
      QStringLiteral("Keyboard Trim Source"), 4.0,
      QStringLiteral("poster://keyboard-trim"));
  if (!keyboard_trim_controller.nudgeSelectedTrimEdge(
          QStringLiteral("start"), 1, false, false) ||
      !nearly_equal(keyboard_trim_controller.currentClip()
                        .value(QStringLiteral("startSeconds"))
                        .toDouble(),
                    0.04)) {
    return fail("keyboard trim should nudge the selected In edge by exactly one sequence frame");
  }
  const auto linked_trim_tracks = keyboard_trim_controller.sequenceTracks();
  const auto linked_trim_audio = linked_trim_tracks.at(1)
                                     .toMap()
                                     .value(QStringLiteral("clips"))
                                     .toList()
                                     .first()
                                     .toMap();
  if (!nearly_equal(linked_trim_audio.value(QStringLiteral("startSeconds")).toDouble(), 0.04)) {
    return fail("ordinary keyboard trimming should keep linked picture and sound edges aligned");
  }
  if (!keyboard_trim_controller.nudgeSelectedTrimEdge(
          QStringLiteral("end"), -5, false, false) ||
      !nearly_equal(keyboard_trim_controller.currentClip()
                        .value(QStringLiteral("endSeconds"))
                        .toDouble(),
                    3.80)) {
    return fail("large keyboard trim should nudge the selected Out edge by exactly five sequence frames");
  }

  TimelineDocumentController timecode_controller;
  timecode_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Direct Timecode")},
      {QStringLiteral("width"), 1920},
      {QStringLiteral("height"), 1080},
      {QStringLiteral("frameRate"), 25.0},
  });
  timecode_controller.addClipFromGallery(
      QStringLiteral("video:timecode-entry"), first_video,
      QStringLiteral("Timecode Source"), 10.0,
      QStringLiteral("poster://timecode-entry"));
  if (!timecode_controller.applyTimecodeEntry(
          QStringLiteral("seek"), QStringLiteral("00:00:02:12")) ||
      !nearly_equal(timecode_controller.sequencePlayheadSeconds(), 2.48) ||
      !timecode_controller.applyTimecodeEntry(
          QStringLiteral("seek"), QStringLiteral("+00:00:00:02")) ||
      !nearly_equal(timecode_controller.sequencePlayheadSeconds(), 2.56)) {
    return fail("direct timecode seeking should support exact frames and signed relative offsets");
  }
  if (!timecode_controller.applyTimecodeEntry(
          QStringLiteral("place"), QStringLiteral("00:00:05:00")) ||
      !nearly_equal(timecode_controller.currentClip()
                        .value(QStringLiteral("startSeconds"))
                        .toDouble(),
                    5.0) ||
      !timecode_controller.applyTimecodeEntry(
          QStringLiteral("trim-start"), QStringLiteral("+00:00:00:02")) ||
      !nearly_equal(timecode_controller.currentClip()
                        .value(QStringLiteral("startSeconds"))
                        .toDouble(),
                    5.08) ||
      !timecode_controller.applyTimecodeEntry(
          QStringLiteral("trim-end"), QStringLiteral("00:00:09:00")) ||
      !nearly_equal(timecode_controller.currentClip()
                        .value(QStringLiteral("endSeconds"))
                        .toDouble(),
                    9.0)) {
    return fail("direct timecode should place clips and set In/Out edit boundaries on exact frames");
  }
  if (!timecode_controller.applyTimecodeEntry(
          QStringLiteral("duration"), QStringLiteral("00:00:02:00")) ||
      !nearly_equal(timecode_controller.currentClip()
                        .value(QStringLiteral("trimmedDurationSeconds"))
                        .toDouble(),
                    2.0)) {
    return fail("direct timecode should set an exact selected-clip duration");
  }
  timecode_controller.undo();
  if (!nearly_equal(timecode_controller.currentClip()
                        .value(QStringLiteral("endSeconds"))
                        .toDouble(),
                    9.0)) {
    return fail("direct timecode duration edits should undo atomically");
  }
  timecode_controller.redo();
  if (!timecode_controller.applyTimecodeEntry(
          QStringLiteral("marker"), QStringLiteral("00:00:01:00")) ||
      timecode_controller.sequenceMarkers().isEmpty() ||
      !nearly_equal(timecode_controller.sequenceMarkers()
                        .first()
                        .toMap()
                        .value(QStringLiteral("seconds"))
                        .toDouble(),
                    1.0) ||
      !timecode_controller.saveTimeline()) {
    return fail("direct timecode should place an undoable marker and save the resulting edit state");
  }
  TimelineDocumentController reopened_timecode_controller;
  if (!reopened_timecode_controller.loadTimeline(timecode_controller.savedDocumentPath()) ||
      !nearly_equal(reopened_timecode_controller.currentClip()
                        .value(QStringLiteral("startSeconds"))
                        .toDouble(),
                    5.08) ||
      !nearly_equal(reopened_timecode_controller.currentClip()
                        .value(QStringLiteral("trimmedDurationSeconds"))
                        .toDouble(),
                    2.0) ||
      reopened_timecode_controller.sequenceMarkers().size() != 1) {
    return fail("direct timecode placement, duration, and marker edits should survive save and reopen");
  }

  if (!controller.splitSelectedClip(5.0)) {
    return fail("splitting the selected clip should succeed inside the trim range");
  }

  if (controller.clipCount() != 3) {
    return fail("splitting should insert a second clip segment into the sequence");
  }

  const auto sequence_tracks = controller.sequenceTracks();
  if (sequence_tracks.size() != 2 ||
      sequence_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 3 ||
      sequence_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 3) {
    return fail("splitting should keep the derived audio lane aligned with the video track");
  }

  const auto clips_after_split = controller.clips();
  if (!nearly_equal(clips_after_split.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 5.0) ||
      !nearly_equal(clips_after_split.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble(), 5.0) ||
      !nearly_equal(clips_after_split.at(1).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 8.5)) {
    return fail("splitting should preserve both halves of the trimmed clip exactly");
  }

  controller.seekSequenceSeconds(6.0);
  if (controller.currentIndex() != 1 ||
      !nearly_equal(controller.currentClipPlayheadSeconds(), 8.0) ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 6.0)) {
    return fail("native sequence seeking should map sequence time onto the correct split clip");
  }

  controller.playSequence();
  if (!controller.sequencePlaying()) {
    return fail("playSequence should mark the timeline as playing");
  }

  controller.advancePlaybackAfterClipEnd();
  if (controller.currentIndex() != -1 || !controller.sequencePlaying() ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 6.5)) {
    return fail("advancing after a clip end should preserve real gap time instead of skipping to the next clip");
  }
  controller.syncGapPlaybackPosition(12.0);
  if (controller.currentIndex() != 2 || !controller.sequencePlaying() ||
      !nearly_equal(controller.currentClipPlayheadSeconds(), 0.0) ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 12.0)) {
    return fail("gap playback should resume the next active clip without stopping the sequence clock");
  }

  controller.stepSequenceFrames(1);
  if (controller.sequencePlaying()) {
    return fail("frame stepping should pause sequence playback");
  }
  if (controller.currentClipPlayheadSeconds() <= 0.0) {
    return fail("frame stepping should advance the native clip playhead");
  }

  controller.selectClip(1);
  controller.moveSelectedForward();
  if (controller.currentIndex() != 2) {
    return fail("moving a clip forward should update the selection index");
  }

  const auto clips = controller.clips();
  if (clips.at(2).toMap().value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Clip One")) {
    return fail("moving a clip forward should reorder the timeline");
  }

  controller.removeSelectedClip();
  if (controller.clipCount() != 2) {
    return fail("removing the selected clip should shrink the sequence");
  }

  if (!controller.dirty()) {
    return fail("timeline edits should mark the document dirty");
  }

  TimelineDocumentController layered_controller;
  layered_controller.createTimeline(QStringLiteral("Layered Timeline"));
  layered_controller.addClipFromGallery(QStringLiteral("video:bg"), first_video, QStringLiteral("Background"), 4.0, QStringLiteral("poster://bg"));
  layered_controller.addClipFromGallery(QStringLiteral("video:fg"), second_video, QStringLiteral("Foreground"), 5.0, QStringLiteral("poster://fg"));
  layered_controller.addVideoTrack();
  layered_controller.selectClip(1);
  layered_controller.moveSelectedClipToTrackRow(1, 1.0);
  if (layered_controller.trackCount() != 4 || layered_controller.videoTrackCount() != 2 || layered_controller.audioTrackCount() != 2) {
    return fail("adding a video track should create a paired video and audio lane");
  }
  const auto layered_tracks = layered_controller.sequenceTracks();
  if (layered_tracks.size() != 4 ||
      layered_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      layered_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
    return fail("moving a clip to a new video lane should preserve clip counts across paired tracks");
  }
  layered_controller.seekSequenceSeconds(1.5);
  if (layered_controller.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      layered_controller.currentClip().value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Foreground") ||
      !nearly_equal(layered_controller.currentClipPlayheadSeconds(), 0.5)) {
    return fail("sequence seeking should resolve the topmost active clip after moving it to a higher track");
  }
  layered_controller.toggleTrackTargeted(1);
  layered_controller.toggleTrackLocked(1);
  layered_controller.toggleTrackVisibility(1);
  const auto targeted_track_state = layered_controller.sequenceTracks().at(1).toMap();
  if (!targeted_track_state.value(QStringLiteral("targeted")).toBool() ||
      !targeted_track_state.value(QStringLiteral("locked")).toBool() ||
      targeted_track_state.value(QStringLiteral("visible")).toBool()) {
    return fail("track state toggles should persist on the selected lane");
  }
  layered_controller.toggleTrackVisibility(1);
  layered_controller.toggleTrackMuted(3);
  layered_controller.seekSequenceSeconds(1.5);
  if (layered_controller.currentClip().value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V2") ||
      layered_controller.currentClip().value(QStringLiteral("audioPlaybackEnabled")).toBool()) {
    return fail("audio mute should silence playback without hiding the visible video clip");
  }

  TimelineDocumentController track_edit_controller;
  track_edit_controller.createTimeline(QStringLiteral("Track Editing Timeline"));
  track_edit_controller.addClipFromGallery(
      QStringLiteral("video:track-one"), first_video, QStringLiteral("Track One"), 4.0,
      QStringLiteral("poster://track-one"));
  track_edit_controller.addVideoTrack();
  track_edit_controller.addClipFromGallery(
      QStringLiteral("video:track-two"), second_video, QStringLiteral("Track Two"), 4.0,
      QStringLiteral("poster://track-two"));
  track_edit_controller.selectClip(1);
  track_edit_controller.moveSelectedClipToTrackRow(1, 0.0);

  auto track_edit_rows = track_edit_controller.sequenceTracks();
  const auto original_first_track_id =
      track_edit_rows.at(0).toMap().value(QStringLiteral("trackId")).toString();
  const auto original_second_track_id =
      track_edit_rows.at(1).toMap().value(QStringLiteral("trackId")).toString();
  if (!track_edit_controller.moveTrackRow(1, -1)) {
    return fail("a populated video/audio track pair should move toward the start of the stack");
  }
  track_edit_rows = track_edit_controller.sequenceTracks();
  if (track_edit_rows.at(0).toMap().value(QStringLiteral("trackId")).toString() !=
          original_second_track_id ||
      track_edit_rows.at(1).toMap().value(QStringLiteral("trackId")).toString() !=
          original_first_track_id ||
      track_edit_rows.at(0).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("V1") ||
      track_edit_rows.at(2).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("A1")) {
    return fail("track reordering should preserve track identity while relabeling paired lanes");
  }
  track_edit_controller.undo();
  track_edit_rows = track_edit_controller.sequenceTracks();
  if (track_edit_rows.at(0).toMap().value(QStringLiteral("trackId")).toString() !=
          original_first_track_id ||
      track_edit_rows.at(1).toMap().value(QStringLiteral("trackId")).toString() !=
          original_second_track_id) {
    return fail("one undo should restore the previous paired track order");
  }
  if (track_edit_controller.removeTrackRow(1, false)) {
    return fail("deleting a populated track pair should require explicit confirmation");
  }
  if (!track_edit_controller.removeTrackRow(1, true) ||
      track_edit_controller.videoTrackCount() != 1 ||
      track_edit_controller.audioTrackCount() != 1 ||
      track_edit_controller.trackCount() != 2) {
    return fail("confirmed track deletion should remove exactly one paired video/audio lane");
  }
  track_edit_controller.undo();
  track_edit_rows = track_edit_controller.sequenceTracks();
  if (track_edit_controller.trackCount() != 4 ||
      track_edit_rows.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      track_edit_rows.at(3).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
    return fail("undo should restore a deleted track pair and all of its linked clip content");
  }

  TimelineDocumentController clip_feedback_controller;
  clip_feedback_controller.createTimeline(QStringLiteral("Clip Feedback Editing"));
  clip_feedback_controller.addClipFromGallery(
      QStringLiteral("video:feedback-a"), first_video, QStringLiteral("Feedback A"), 2.0,
      QStringLiteral("poster://feedback-a"));
  clip_feedback_controller.addClipFromGallery(
      QStringLiteral("video:feedback-b"), second_video, QStringLiteral("Feedback B"), 2.0,
      QStringLiteral("poster://feedback-b"));
  clip_feedback_controller.addClipFromGallery(
      QStringLiteral("video:feedback-c"), first_video, QStringLiteral("Feedback C"), 2.0,
      QStringLiteral("poster://feedback-c"));
  clip_feedback_controller.selectClipRange(0, 1);
  if (!clip_feedback_controller.groupSelectedClips()) {
    return fail("two selected clips should form one persistent editorial group");
  }
  auto grouped_clips = clip_feedback_controller.clips();
  if (grouped_clips.at(0).toMap().value(QStringLiteral("groupedClipCount")).toInt() != 2 ||
      grouped_clips.at(1).toMap().value(QStringLiteral("groupedClipCount")).toInt() != 2) {
    return fail("grouped clips should expose a quiet relationship count to the timeline UI");
  }
  clip_feedback_controller.undo();
  grouped_clips = clip_feedback_controller.clips();
  if (grouped_clips.at(0).toMap().value(QStringLiteral("groupedClipCount")).toInt() != 1) {
    return fail("one undo should restore independent clip grouping");
  }
  clip_feedback_controller.redo();
  if (!clip_feedback_controller.ungroupSelectedClips() ||
      clip_feedback_controller.clips().at(0).toMap()
              .value(QStringLiteral("groupedClipCount")).toInt() != 1) {
    return fail("ungroup should restore independent edit relationships");
  }

  clip_feedback_controller.selectClip(1);
  if (!clip_feedback_controller.moveSelectedClipToTrackRowWithMode(
          0, 0.0, QStringLiteral("overwrite"), false) ||
      clip_feedback_controller.clipCount() != 2 ||
      clip_feedback_controller.currentClip().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Feedback B") ||
      !nearly_equal(clip_feedback_controller.currentClip()
                        .value(QStringLiteral("startSeconds")).toDouble(),
                    0.0)) {
    return fail("overwrite move should replace the destination range with the dragged clip");
  }
  clip_feedback_controller.undo();
  clip_feedback_controller.selectClip(1);
  if (!clip_feedback_controller.moveSelectedClipToTrackRowWithMode(
          0, 0.0, QStringLiteral("insert"), false) ||
      clip_feedback_controller.clipCount() != 3) {
    return fail("insert move should preserve all clips while opening the destination range");
  }
  auto feedback_inserted_clips = clip_feedback_controller.clips();
  if (feedback_inserted_clips.at(0).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Feedback B") ||
      !nearly_equal(feedback_inserted_clips.at(1).toMap()
                        .value(QStringLiteral("startSeconds")).toDouble(),
                    2.0) ||
      !nearly_equal(feedback_inserted_clips.at(2).toMap()
                        .value(QStringLiteral("startSeconds")).toDouble(),
                    6.0)) {
    return fail("insert move should preview and commit the exact downstream displacement");
  }
  clip_feedback_controller.undo();
  clip_feedback_controller.selectClip(1);
  if (!clip_feedback_controller.moveSelectedClipToTrackRowWithMode(
          0, 6.0, QStringLiteral("insert"), true)) {
    return fail("ripple move should commit after closing the clip's source gap");
  }
  const auto ripple_moved_clips = clip_feedback_controller.clips();
  if (ripple_moved_clips.size() != 3 ||
      ripple_moved_clips.at(0).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Feedback A") ||
      ripple_moved_clips.at(1).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Feedback C") ||
      ripple_moved_clips.at(2).toMap().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Feedback B") ||
      !nearly_equal(ripple_moved_clips.at(1).toMap()
                        .value(QStringLiteral("startSeconds")).toDouble(),
                    2.0) ||
      !nearly_equal(ripple_moved_clips.at(2).toMap()
                        .value(QStringLiteral("startSeconds")).toDouble(),
                    4.0)) {
    return fail("ripple move should close the source gap before opening the destination range");
  }

  TimelineDocumentController source_controller;
  source_controller.createTimeline(QStringLiteral("Source Timeline"));
  source_controller.loadSourceClipFromGallery(
      QStringLiteral("video:src"),
      first_video,
      QStringLiteral("Source Clip"),
      5.0,
      QStringLiteral("poster://src"));
  if (!wait_for_media_analysis(&source_controller)) {
    return fail("source media analysis should finish without blocking the controller");
  }
  source_controller.setSourceInSeconds(1.0);
  source_controller.setSourceOutSeconds(3.0);
  source_controller.appendSourceClipToTimeline();
  source_controller.seekSequenceSeconds(0.75);
  const auto beat_marker_id = source_controller.addSequenceMarker(QStringLiteral("Beat"));
  source_controller.setSequenceMarkerNote(beat_marker_id, QStringLiteral("Review this cut"));
  source_controller.setSequenceMarkerStatus(beat_marker_id, QStringLiteral("review"));
  source_controller.setSequenceMarkerCompleted(beat_marker_id, true);
  source_controller.seekSequenceSeconds(1.5);
  source_controller.addSequenceMarker(QStringLiteral("Outro"));
  if (source_controller.clipCount() != 1 ||
      !nearly_equal(source_controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(source_controller.currentClip().value(QStringLiteral("trimEndSeconds")).toDouble(), 3.0) ||
      source_controller.sequenceMarkers().size() != 2) {
    return fail("source monitor edits and sequence markers should persist in controller state");
  }
  const auto source_markers = source_controller.sequenceMarkers();
  if (source_markers.at(0).toMap().value(QStringLiteral("note")).toString() != QStringLiteral("Review this cut") ||
      source_markers.at(0).toMap().value(QStringLiteral("status")).toString() != QStringLiteral("review") ||
      !source_markers.at(0).toMap().value(QStringLiteral("completed")).toBool()) {
    return fail("marker notes, status, and completion state should persist in controller state");
  }
  source_controller.selectClip(0);
  source_controller.addSelectedClipEffect(QStringLiteral("Exposure"));
  const auto review_search = source_controller.timelineSearchResults(
      QStringLiteral("Review this cut"),
      QStringLiteral("markers"),
      QString{},
      QStringLiteral("review"));
  const auto open_marker_search = source_controller.timelineSearchResults(
      QString{},
      QStringLiteral("markers"),
      QString{},
      QStringLiteral("open"));
  const auto effect_search = source_controller.timelineSearchResults(
      QStringLiteral("Exposure"), QStringLiteral("effects"));
  const auto selected_track_id = source_controller.currentClip()
                                     .value(QStringLiteral("trackId"))
                                     .toString();
  const auto selected_track_search = source_controller.timelineSearchResults(
      QString{}, QStringLiteral("clips"), selected_track_id);
  if (review_search.size() != 1 ||
      review_search.first().toMap().value(QStringLiteral("id")).toString() != beat_marker_id ||
      open_marker_search.size() != 1 ||
      open_marker_search.first().toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Outro") ||
      effect_search.size() != 1 || selected_track_search.size() != 1 ||
      selected_track_search.first().toMap().value(QStringLiteral("trackId")).toString() !=
          selected_track_id) {
    return fail("timeline search should filter marker notes/status, effects, and selected-track scope deterministically");
  }
  const auto effect_result = effect_search.first().toMap();
  if (!source_controller.activateTimelineSearchResult(
          effect_result.value(QStringLiteral("type")).toString(),
          effect_result.value(QStringLiteral("id")).toString()) ||
      source_controller.currentClip().value(QStringLiteral("clipId")).toString() !=
          effect_result.value(QStringLiteral("id")).toString()) {
    return fail("activating a clip search result should select it and seek its exact start");
  }
  const auto review_result = review_search.first().toMap();
  if (!source_controller.activateTimelineSearchResult(
          review_result.value(QStringLiteral("type")).toString(),
          review_result.value(QStringLiteral("id")).toString()) ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 0.75)) {
    return fail("activating a marker search result should seek its exact sequence time");
  }
  source_controller.seekSequenceSeconds(0.0);
  if (!source_controller.seekNextSequenceMarker() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 0.75)) {
    return fail("next marker navigation should seek to the next marker after the current playhead");
  }
  source_controller.seekSequenceSeconds(1.6);
  if (!source_controller.seekPreviousSequenceMarker() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 1.5)) {
    return fail("previous marker navigation should seek to the prior marker before the current playhead");
  }
  source_controller.seekSequenceSeconds(0.1);
  if (!source_controller.seekNextEditPoint() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 2.0)) {
    return fail("next edit navigation should seek to the next clip boundary in sequence time");
  }
  source_controller.seekSequenceSeconds(1.9);
  if (!source_controller.seekPreviousEditPoint() ||
      !nearly_equal(source_controller.sequencePlayheadSeconds(), 0.0)) {
    return fail("previous edit navigation should seek to the prior clip boundary in sequence time");
  }
  double requested_source_seconds = -1.0;
  QObject::connect(
      &source_controller,
      &TimelineDocumentController::sourceSeekRequested,
      [&requested_source_seconds](double seconds) { requested_source_seconds = seconds; });
  if (!source_controller.seekSourceInPoint() ||
      !nearly_equal(requested_source_seconds, source_controller.sourceInSeconds())) {
    return fail("source In navigation should request the exact marked source frame");
  }
  if (!source_controller.seekSourceOutPoint() ||
      !nearly_equal(requested_source_seconds, source_controller.sourceOutSeconds())) {
    return fail("source Out navigation should request the exact marked source frame");
  }
  source_controller.seekSequenceSeconds(0.75);
  if (!source_controller.matchFrameToSource() ||
      source_controller.sourceClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("video:src") ||
      !nearly_equal(source_controller.sourceInSeconds(), 1.0) ||
      !nearly_equal(source_controller.sourceOutSeconds(), 3.0) ||
      !nearly_equal(requested_source_seconds, 1.75)) {
    return fail("Match Frame should load the active master clip and seek Source to the exact timeline frame");
  }
  const auto gang_dirty_before = source_controller.dirty();
  const auto gang_state = source_controller.synchronizeSourceToProgram(0.75);
  if (!gang_state.value(QStringLiteral("matched")).toBool() ||
      !gang_state.value(QStringLiteral("active")).toBool() ||
      gang_state.value(QStringLiteral("clipId")).toString().isEmpty() ||
      gang_state.value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V1") ||
      !nearly_equal(gang_state.value(QStringLiteral("sequenceSeconds")).toDouble(), 0.75) ||
      !nearly_equal(gang_state.value(QStringLiteral("sourceSeconds")).toDouble(), 1.75) ||
      !nearly_equal(requested_source_seconds, 1.75) ||
      source_controller.dirty() != gang_dirty_before) {
    return fail("ganged Program-to-Source sync should publish the exact active clip frame without editing the sequence");
  }
  const auto reverse_gang =
      source_controller.gangedProgramPositionForSource(2.25);
  if (!reverse_gang.value(QStringLiteral("matched")).toBool() ||
      !reverse_gang.value(QStringLiteral("frameExact")).toBool() ||
      reverse_gang.value(QStringLiteral("clamped")).toBool() ||
      !nearly_equal(
          reverse_gang.value(QStringLiteral("resolvedSourceSeconds")).toDouble(), 2.25) ||
      !nearly_equal(
          reverse_gang.value(QStringLiteral("sequenceSeconds")).toDouble(), 1.25) ||
      source_controller.dirty() != gang_dirty_before) {
    return fail("ganged Source scrubbing should invert the edit mapping back to Program without document mutation");
  }
  const auto gap_gang = source_controller.synchronizeSourceToProgram(2.0);
  const auto stale_reverse_gang =
      source_controller.gangedProgramPositionForSource(2.25);
  if (gap_gang.value(QStringLiteral("matched")).toBool() ||
      gap_gang.value(QStringLiteral("status")).toString() != QStringLiteral("Program gap") ||
      stale_reverse_gang.value(QStringLiteral("matched")).toBool() ||
      !source_controller.hasSourceClip() || source_controller.dirty() != gang_dirty_before) {
    return fail("ganged monitors should disclose a Program gap, retain the last Source frame, and reject stale reverse mapping");
  }
  source_controller.clearProgramSourceGangState();
  if (source_controller.programSourceGangState()
          .value(QStringLiteral("active")).toBool() ||
      source_controller.programSourceGangState()
          .value(QStringLiteral("matched")).toBool() ||
      source_controller.dirty() != gang_dirty_before) {
    return fail("unganging monitors should clear only session state");
  }
  source_controller.setSourceInSeconds(1.25);
  source_controller.setSourceOutSeconds(2.75);
  source_controller.clearSourceInPoint();
  if (!nearly_equal(source_controller.sourceInSeconds(), 0.0) ||
      !nearly_equal(source_controller.sourceOutSeconds(), 2.75)) {
    return fail("clearing Source In should preserve the independently marked Out point");
  }
  source_controller.setSourceInSeconds(1.0);
  const auto full_source_duration = source_controller.sourceClip()
                                        .value(QStringLiteral("durationSeconds"))
                                        .toDouble();
  source_controller.clearSourceOutPoint();
  if (!nearly_equal(source_controller.sourceInSeconds(), 1.0) ||
      !nearly_equal(source_controller.sourceOutSeconds(), full_source_duration)) {
    return fail("clearing Source Out should preserve In and restore the source end");
  }

  TimelineDocumentController review_controller;
  review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Client Review Exchange")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  review_controller.addClipFromGallery(
      QStringLiteral("photo:review"), photo_still, QStringLiteral("Review Frame"),
      1.5, photo_still);
  review_controller.seekSequenceSeconds(1.041);
  const auto review_note_id = review_controller.addReviewNote(
      QStringLiteral("Tighten opening"), QStringLiteral("Lose two frames before the reveal."),
      QStringLiteral("Editorial"), QStringLiteral("open"));
  const auto review_notes = review_controller.reviewNotes();
  if (review_note_id.isEmpty() || review_notes.size() != 1 ||
      review_controller.sequenceMarkers().size() != 1 ||
      !review_notes.first().toMap().value(QStringLiteral("reviewNote")).toBool() ||
      review_notes.first().toMap().value(QStringLiteral("assignee")).toString() !=
          QStringLiteral("Editorial") ||
      review_notes.first().toMap().value(QStringLiteral("approvalState")).toString() !=
          QStringLiteral("open") ||
      !nearly_equal(
          review_notes.first().toMap().value(QStringLiteral("seconds")).toDouble(),
          25.0 / 24.0, 0.0001)) {
    return fail("review notes should be frame-quantized markers with assignment and approval state");
  }
  if (!review_controller.updateSequenceMarkerReview(
          review_note_id, QStringLiteral("Tighten opening"),
          QStringLiteral("Lose two frames before the reveal."), QStringLiteral("review"),
          QStringLiteral("Picture Editor"), QStringLiteral("changes-requested"), false)) {
    return fail("review assignment and approval should update atomically");
  }
  review_controller.undo();
  if (review_controller.reviewNotes().first().toMap()
          .value(QStringLiteral("assignee")).toString() != QStringLiteral("Editorial") ||
      review_controller.reviewNotes().first().toMap()
          .value(QStringLiteral("approvalState")).toString() != QStringLiteral("open")) {
    std::cerr << "Review undo fields: assignee="
              << review_controller.reviewNotes().first().toMap()
                     .value(QStringLiteral("assignee")).toString().toStdString()
              << " approval="
              << review_controller.reviewNotes().first().toMap()
                     .value(QStringLiteral("approvalState")).toString().toStdString()
              << '\n';
    return fail("one undo should restore every field in a review-note edit");
  }
  review_controller.redo();
  const auto review_package_path = workspace.filePath(QStringLiteral("client-review.json"));
  if (review_controller.exportReviewNotes(review_package_path) != review_package_path) {
    return fail("review notes should export to an explicit portable JSON package");
  }
  QFile review_package_file(review_package_path);
  if (!review_package_file.open(QIODevice::ReadOnly)) {
    return fail("the exported review package should be readable");
  }
  const auto review_package = QJsonDocument::fromJson(review_package_file.readAll()).object();
  const auto exported_review_notes = review_package.value(QStringLiteral("notes")).toArray();
  if (review_package.value(QStringLiteral("format")).toString() !=
          QStringLiteral("framehouse-review-notes") ||
      review_package.value(QStringLiteral("version")).toInt() != 1 ||
      !nearly_equal(review_package.value(QStringLiteral("frameRate")).toDouble(), 24.0) ||
      exported_review_notes.size() != 1 ||
      exported_review_notes.first().toObject().value(QStringLiteral("frame")).toInt() != 25 ||
      exported_review_notes.first().toObject().value(QStringLiteral("assignee")).toString() !=
          QStringLiteral("Picture Editor") ||
      exported_review_notes.first().toObject().value(QStringLiteral("approvalState")).toString() !=
          QStringLiteral("changes-requested")) {
    return fail("review exchange should preserve version, frame rate, exact frame, assignment, and approval");
  }

  TimelineDocumentController imported_review_controller;
  imported_review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Imported Client Review")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  imported_review_controller.addClipFromGallery(
      QStringLiteral("photo:review-copy"), photo_still, QStringLiteral("Review Copy"),
      1.5, photo_still);
  imported_review_controller.addSequenceMarker(QStringLiteral("Local marker"));
  if (imported_review_controller.importReviewNotes(review_package_path) != 1 ||
      imported_review_controller.reviewNotes().size() != 1 ||
      imported_review_controller.sequenceMarkers().size() != 2 ||
      imported_review_controller.importReviewNotes(review_package_path) != 1 ||
      imported_review_controller.reviewNotes().size() != 1) {
    return fail("review import should merge by exchange id without duplicating notes or replacing local markers");
  }

  auto legacy_review_package = review_package;
  legacy_review_package.insert(QStringLiteral("format"), QStringLiteral("stillfold-review-notes"));
  const auto legacy_review_package_path = workspace.filePath(QStringLiteral("legacy-client-review.json"));
  QFile legacy_review_package_file(legacy_review_package_path);
  const auto legacy_review_package_bytes =
      QJsonDocument(legacy_review_package).toJson(QJsonDocument::Compact);
  if (!legacy_review_package_file.open(QIODevice::WriteOnly) ||
      legacy_review_package_file.write(legacy_review_package_bytes) !=
          legacy_review_package_bytes.size()) {
    return fail("the legacy review package fixture should be writable");
  }
  legacy_review_package_file.close();

  TimelineDocumentController legacy_review_controller;
  legacy_review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Legacy Client Review")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  legacy_review_controller.addClipFromGallery(
      QStringLiteral("photo:legacy-review-copy"), photo_still,
      QStringLiteral("Legacy Review Copy"), 1.5, photo_still);
  if (legacy_review_controller.importReviewNotes(legacy_review_package_path) != 1 ||
      legacy_review_controller.reviewNotes().size() != 1) {
    return fail("Looksy should import version 1 Stillfold review packages without migration");
  }

  imported_review_controller.undo();
  imported_review_controller.undo();
  if (!imported_review_controller.reviewNotes().isEmpty() ||
      imported_review_controller.sequenceMarkers().size() != 1) {
    return fail("each review package import should be one undoable document change");
  }
  imported_review_controller.redo();
  if (imported_review_controller.reviewNotes().size() != 1 ||
      !imported_review_controller.saveTimeline()) {
    return fail("imported review notes should redo and persist with the timeline");
  }
  if (!removePersistedJobRecord(
          QStringLiteral("timeline-render-%1").arg(
              imported_review_controller.timelineAssetId().section(':', 1)))) {
    return fail("the review persistence fixture should clean up its unrelated gallery render job");
  }
  TimelineDocumentController reopened_review_controller;
  if (!reopened_review_controller.loadTimeline(
          imported_review_controller.savedDocumentPath()) ||
      reopened_review_controller.reviewNotes().size() != 1 ||
      reopened_review_controller.reviewNotes().first().toMap()
              .value(QStringLiteral("approvalState")).toString() !=
          QStringLiteral("changes-requested")) {
    return fail("review assignment and approval state should survive save and reopen");
  }
  TimelineDocumentController mismatched_review_controller;
  mismatched_review_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Mismatched Review")},
      {QStringLiteral("frameRate"), 30.0},
  });
  if (mismatched_review_controller.importReviewNotes(review_package_path) != 0 ||
      !mismatched_review_controller.status().contains(QStringLiteral("frame rate"))) {
    return fail("review exchange should reject frame-rate drift instead of silently moving notes");
  }

  const auto color_source_path = workspace.filePath(QStringLiteral("color-source.png"));
  const auto color_reference_path = workspace.filePath(QStringLiteral("color-reference.png"));
  QImage color_source(64, 64, QImage::Format_RGB32);
  color_source.fill(qRgb(72, 118, 176));
  QImage color_reference(64, 64, QImage::Format_RGB32);
  color_reference.fill(qRgb(144, 88, 96));
  if (!color_source.save(color_source_path) || !color_reference.save(color_reference_path)) {
    return fail("the color finishing fixture images should be writable");
  }
  const auto blue_lut_path = workspace.filePath(QStringLiteral("solid-blue.cube"));
  QFile blue_lut(blue_lut_path);
  if (!blue_lut.open(QIODevice::WriteOnly) ||
      blue_lut.write(
          "TITLE \"Solid Blue\"\nLUT_3D_SIZE 2\n"
          "0 0 1\n0 0 1\n0 0 1\n0 0 1\n"
          "0 0 1\n0 0 1\n0 0 1\n0 0 1\n") <= 0) {
    return fail("the CUBE LUT fixture should be writable");
  }
  blue_lut.close();
  TimelineDocumentController color_controller;
  color_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Color Finishing Truth")},
      {QStringLiteral("width"), 64},
      {QStringLiteral("height"), 64},
      {QStringLiteral("frameRate"), 24.0},
  });
  color_controller.addClipFromGallery(
      QStringLiteral("photo:color-source"), color_source_path,
      QStringLiteral("Cool Source"), 1.0, color_source_path);
  color_controller.addClipFromGallery(
      QStringLiteral("photo:color-reference"), color_reference_path,
      QStringLiteral("Warm Reference"), 1.0, color_reference_path);
  const auto color_reference_id = color_controller.clips().at(1).toMap()
                                      .value(QStringLiteral("clipId")).toString();
  color_controller.selectClip(0);
  if (!color_controller.matchSelectedClipColorToReference(color_reference_id) ||
      nearly_equal(color_controller.currentClip().value(QStringLiteral("shotMatchRedGain")).toDouble(), 1.0) ||
      nearly_equal(color_controller.currentClip().value(QStringLiteral("shotMatchBlueGain")).toDouble(), 1.0)) {
    return fail("shot matching should derive real per-channel gains from the chosen reference frame");
  }
  color_controller.undo();
  if (!nearly_equal(color_controller.currentClip().value(QStringLiteral("shotMatchRedGain")).toDouble(), 1.0)) {
    return fail("shot matching should be one undoable color finishing edit");
  }
  color_controller.redo();
  if (!color_controller.applySelectedClipColorLut(blue_lut_path) ||
      color_controller.currentClip().value(QStringLiteral("colorLutName")).toString() !=
          QStringLiteral("solid-blue")) {
    return fail("a validated CUBE LUT should attach to the selected clip");
  }
  const auto color_scopes = color_controller.analyzeSelectedClipColorScopes();
  if (color_scopes.value(QStringLiteral("sampleCount")).toInt() <= 0 ||
      color_scopes.value(QStringLiteral("luma")).toList().size() != 64 ||
      color_scopes.value(QStringLiteral("red")).toList().size() != 64 ||
      color_scopes.value(QStringLiteral("lumaWaveform")).toList().size() != 4096 ||
      color_scopes.value(QStringLiteral("vectorscope")).toList().size() != 4096 ||
      color_scopes.value(QStringLiteral("lutName")).toString() != QStringLiteral("solid-blue")) {
    return fail("RGB and luma scopes should analyze the same post-grade filter chain used by export");
  }
  const auto color_render_path = workspace.filePath(QStringLiteral("color-finished.mp4"));
  if (!color_controller.renderCurrentTimeline(color_render_path)) {
    return fail("the LUT and shot-match filter chain should render through the ordinary timeline exporter");
  }
  const auto color_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  QProcess color_sample;
  color_sample.start(
      color_ffmpeg,
      {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), color_render_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"),
       QStringLiteral("image2pipe"), QStringLiteral("-vcodec"), QStringLiteral("png"),
       QStringLiteral("pipe:1")});
  if (!color_sample.waitForFinished(30000)) {
    return fail("the color finishing render should decode for pixel verification");
  }
  const auto color_render_frame = QImage::fromData(color_sample.readAllStandardOutput(), "PNG");
  const auto color_render_pixel = color_render_frame.pixelColor(32, 32);
  if (color_render_pixel.blue() < 220 || color_render_pixel.red() > 35 ||
      color_render_pixel.green() > 35) {
    return fail("CUBE LUT pixels should match the post-grade scope and final export path");
  }
  const auto color_cache_started = color_controller.generateRenderCache();
  const auto color_cache_ready = color_cache_started && wait_for_render_cache(&color_controller);
  if (!color_cache_ready) {
    std::cerr << "color cache started=" << color_cache_started
              << " status=" << color_controller.renderCacheStatus().toStdString()
              << " message=" << color_controller.status().toStdString() << '\n';
    return fail("the representative final-pixel Program preview should render into the timeline cache");
  }
  color_controller.seekSequenceSeconds(0.5);
  const auto preview_cache_path =
      QUrl(color_controller.renderCachePlaybackSourceUrl()).toLocalFile();
  QProcess preview_sample;
  preview_sample.start(
      color_ffmpeg,
      {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
       QStringLiteral("0.5"), QStringLiteral("-i"), preview_cache_path,
       QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"),
       QStringLiteral("image2pipe"), QStringLiteral("-vcodec"), QStringLiteral("png"),
       QStringLiteral("pipe:1")});
  if (!preview_sample.waitForFinished(30000)) {
    return fail("the final-pixel Program cache should decode for export parity comparison");
  }
  const auto preview_cache_frame = QImage::fromData(
      preview_sample.readAllStandardOutput(), "PNG");
  const auto preview_cache_pixel = preview_cache_frame.pixelColor(32, 32);
  const auto channel_difference = [](int left, int right) { return qAbs(left - right); };
  if (preview_cache_frame.isNull() ||
      channel_difference(preview_cache_pixel.red(), color_render_pixel.red()) > 4 ||
      channel_difference(preview_cache_pixel.green(), color_render_pixel.green()) > 4 ||
      channel_difference(preview_cache_pixel.blue(), color_render_pixel.blue()) > 4) {
    return fail("the final-pixel Program preview and export should match within codec tolerance");
  }
  if (!color_controller.saveTimeline()) {
    return fail("color finishing settings should save with the timeline");
  }
  if (!removePersistedJobRecord(
          QStringLiteral("timeline-render-%1").arg(
              color_controller.timelineAssetId().section(':', 1)))) {
    return fail("the color persistence fixture should clean up its unrelated gallery render job");
  }
  TimelineDocumentController reopened_color_controller;
  if (!reopened_color_controller.loadTimeline(color_controller.savedDocumentPath()) ||
      reopened_color_controller.currentClip().value(QStringLiteral("colorLutName")).toString() !=
          QStringLiteral("solid-blue") ||
      nearly_equal(reopened_color_controller.currentClip()
                       .value(QStringLiteral("shotMatchRedGain")).toDouble(), 1.0)) {
    return fail("LUT identity and shot-match gains should survive save and reopen");
  }

  TimelineDocumentController clipboard_controller;
  clipboard_controller.createTimeline(QStringLiteral("Editorial Clipboard"));
  clipboard_controller.addClipFromGallery(
      QStringLiteral("video:clipboard-a"), first_video, QStringLiteral("Clipboard A"), 2.0,
      QStringLiteral("poster://clipboard-a"));
  clipboard_controller.addClipFromGallery(
      QStringLiteral("video:clipboard-b"), second_video, QStringLiteral("Clipboard B"), 2.0,
      QStringLiteral("poster://clipboard-b"));
  clipboard_controller.addVideoTrack();
  clipboard_controller.selectClip(1);
  clipboard_controller.moveSelectedClipToTrackRow(1, 1.0);
  clipboard_controller.selectClipRange(0, 1);
  if (clipboard_controller.copySelectedClips() != 2 ||
      clipboard_controller.clipClipboardCount() != 2 ||
      !nearly_equal(clipboard_controller.clipClipboardDurationSeconds(), 3.0) ||
      !clipboard_controller.clipClipboardSummary().contains(QStringLiteral("2 clips"))) {
    return fail("clip copy should preserve a multi-track selection and its complete relative span");
  }
  clipboard_controller.toggleTrackTargeted(1);
  clipboard_controller.seekSequenceSeconds(3.0);
  if (clipboard_controller.pasteClipsAtPlayhead(QStringLiteral("overwrite")) != 2 ||
      clipboard_controller.videoTrackCount() != 3 ||
      clipboard_controller.selectedClipCount() != 2) {
    return fail("overwrite paste should map relative video lanes from the targeted destination and select the result");
  }
  auto clipboard_tracks = clipboard_controller.sequenceTracks();
  const auto pasted_v2 = clipboard_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
  const auto pasted_v3 = clipboard_tracks.at(2).toMap().value(QStringLiteral("clips")).toList();
  if (pasted_v2.size() != 2 || pasted_v3.size() != 1 ||
      !nearly_equal(pasted_v2.last().toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.0) ||
      !nearly_equal(pasted_v3.first().toMap().value(QStringLiteral("startSeconds")).toDouble(), 4.0)) {
    return fail("paste should preserve cross-track offsets and relative clip timing");
  }
  clipboard_controller.undo();
  if (clipboard_controller.videoTrackCount() != 2 || clipboard_controller.clipCount() != 2) {
    return fail("multi-track paste and any required destination tracks should undo atomically");
  }
  clipboard_controller.redo();
  if (clipboard_controller.videoTrackCount() != 3 || clipboard_controller.clipCount() != 4 ||
      clipboard_controller.cutSelectedClips() != 2 || clipboard_controller.clipCount() != 2) {
    return fail("redo should restore pasted selection and Cut should copy then lift it in one document edit");
  }
  clipboard_controller.undo();
  if (clipboard_controller.clipCount() != 4 || clipboard_controller.clipClipboardCount() != 2) {
    return fail("cut should undo without clearing the reusable timeline clipboard");
  }

  TimelineDocumentController insert_paste_controller;
  insert_paste_controller.createTimeline(QStringLiteral("Insert Paste"));
  insert_paste_controller.addClipFromGallery(
      QStringLiteral("video:insert-paste"), first_video, QStringLiteral("Long Clip"), 4.0,
      QStringLiteral("poster://insert-paste"));
  if (insert_paste_controller.copySelectedClips() != 1) {
    return fail("single-clip copy should populate the same editorial clipboard path");
  }
  insert_paste_controller.seekSequenceSeconds(3.0);
  insert_paste_controller.addSequenceMarker(QStringLiteral("After Insert"));
  insert_paste_controller.seekSequenceSeconds(2.0);
  if (insert_paste_controller.pasteClipsAtPlayhead(QStringLiteral("insert")) != 1 ||
      insert_paste_controller.clipCount() != 3 ||
      !nearly_equal(insert_paste_controller.totalDurationSecondsValue(), 8.0) ||
      !nearly_equal(
          insert_paste_controller.sequenceMarkers().first().toMap().value(QStringLiteral("seconds")).toDouble(),
          7.0)) {
    return fail("insert paste should split crossing clips, open a full-sequence gap, and ripple markers");
  }
  insert_paste_controller.undo();
  if (insert_paste_controller.clipCount() != 1 ||
      !nearly_equal(insert_paste_controller.totalDurationSecondsValue(), 4.0) ||
      !nearly_equal(
          insert_paste_controller.sequenceMarkers().first().toMap().value(QStringLiteral("seconds")).toDouble(),
          3.0)) {
    return fail("insert paste should undo the split, global ripple, marker move, and pasted clip atomically");
  }

  TimelineDocumentController pancake_source_controller;
  pancake_source_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Pancake Selects")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 24.0},
  });
  pancake_source_controller.addClipFromGallery(
      QStringLiteral("video:pancake-a"), first_video,
      QStringLiteral("Select A"), 4.0, QStringLiteral("poster://pancake-a"));
  pancake_source_controller.addClipFromGallery(
      QStringLiteral("video:pancake-b"), second_video,
      QStringLiteral("Select B"), 3.0, QStringLiteral("poster://pancake-b"));
  pancake_source_controller.addVideoTrack();
  pancake_source_controller.moveSelectedClipToTrackRow(1, 2.0);
  if (!pancake_source_controller.saveTimeline()) {
    return fail("the upper pancake sequence should save before it is exposed read-only");
  }
  const auto pancake_source_path = pancake_source_controller.savedDocumentPath();
  const auto pancake_source_tracks = pancake_source_controller.sequenceTracks();
  const QStringList pancake_source_clip_ids{
      pancake_source_tracks.at(0).toMap().value(QStringLiteral("clips")).toList()
          .first().toMap().value(QStringLiteral("clipId")).toString(),
      pancake_source_tracks.at(1).toMap().value(QStringLiteral("clips")).toList()
          .first().toMap().value(QStringLiteral("clipId")).toString(),
  };

  TimelineDocumentController pancake_destination_controller;
  pancake_destination_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Pancake Assembly")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 30.0},
  });
  pancake_destination_controller.addClipFromGallery(
      QStringLiteral("video:pancake-base"), first_video,
      QStringLiteral("Assembly Tail"), 2.0,
      QStringLiteral("poster://pancake-base"));
  pancake_destination_controller.moveSelectedClipToTrackRow(0, 12.0);
  if (!pancake_destination_controller.saveTimeline()) {
    return fail("the lower pancake destination should save before dual-sequence editing");
  }
  auto found_pancake_source = false;
  for (const auto& value : pancake_destination_controller.pancakeSequences()) {
    if (value.toMap().value(QStringLiteral("documentPath")).toString() ==
        pancake_source_path) {
      found_pancake_source = true;
      break;
    }
  }
  const auto pancake_sequence_preview =
      pancake_destination_controller.pancakeSequencePreview(pancake_source_path);
  if (!found_pancake_source ||
      !pancake_sequence_preview.value(QStringLiteral("available")).toBool() ||
      pancake_sequence_preview.value(QStringLiteral("name")).toString() !=
          QStringLiteral("Pancake Selects") ||
      pancake_sequence_preview.value(QStringLiteral("clipCount")).toInt() != 2 ||
      pancake_sequence_preview.value(QStringLiteral("tracks")).toList().size() != 4) {
    return fail("pancake mode should discover another saved sequence and expose its complete read-only track graph");
  }
  const auto pancake_copy_preview =
      pancake_destination_controller.pancakeTransferPreview(
          pancake_source_path, pancake_source_clip_ids, 0.0, 0.0,
          1.0, QStringLiteral("overwrite"), QStringLiteral("copy"));
  if (!pancake_copy_preview.value(QStringLiteral("available")).toBool() ||
      pancake_copy_preview.value(QStringLiteral("clipCount")).toInt() != 2 ||
      !pancake_copy_preview.value(QStringLiteral("frameRateConversion")).toBool() ||
      !nearly_equal(
          pancake_copy_preview.value(QStringLiteral("sourceStartSeconds")).toDouble(),
          0.0) ||
      !nearly_equal(
          pancake_copy_preview.value(QStringLiteral("durationSeconds")).toDouble(),
          5.0)) {
    return fail("clip-drag preview should report exact multi-track timing and honest source-to-destination frame-grid conversion");
  }
  if (pancake_destination_controller.transferFromPancakeSequence(
          pancake_source_path, pancake_source_clip_ids, 0.0, 0.0,
          1.0, QStringLiteral("overwrite"), QStringLiteral("copy")) != 2 ||
      pancake_destination_controller.clipCount() != 3 ||
      pancake_destination_controller.videoTrackCount() != 2) {
    return fail("pancake clip transfer should copy both source tracks through one destination edit");
  }
  auto pancake_destination_tracks =
      pancake_destination_controller.sequenceTracks();
  const auto pancake_destination_v1 =
      pancake_destination_tracks.at(0).toMap().value(QStringLiteral("clips")).toList();
  const auto pancake_destination_v2 =
      pancake_destination_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
  if (!nearly_equal(
          pancake_destination_v1.first().toMap()
              .value(QStringLiteral("startSeconds")).toDouble(),
          1.0) ||
      !nearly_equal(
          pancake_destination_v2.first().toMap()
              .value(QStringLiteral("startSeconds")).toDouble(),
          3.0) ||
      pancake_destination_v1.first().toMap()
              .value(QStringLiteral("linkedGroupId")).toString() ==
          pancake_source_tracks.at(0).toMap().value(QStringLiteral("clips")).toList()
              .first().toMap().value(QStringLiteral("linkedGroupId")).toString()) {
    return fail("pancake copy should preserve two-second cross-track spacing while assigning safe new linked identities");
  }
  pancake_destination_controller.undo();
  if (pancake_destination_controller.clipCount() != 1) {
    return fail("one undo should remove the complete multi-track pancake copy");
  }
  pancake_destination_controller.redo();
  if (pancake_destination_controller.clipCount() != 3) {
    return fail("one redo should restore the complete multi-track pancake copy");
  }

  QFile pancake_source_before_file(pancake_source_path);
  if (!pancake_source_before_file.open(QIODevice::ReadOnly)) {
    return fail("the source pancake document should remain readable before a move");
  }
  const auto pancake_source_bytes_before_move = pancake_source_before_file.readAll();
  const auto pancake_range_preview =
      pancake_destination_controller.pancakeTransferPreview(
          pancake_source_path, {}, 1.0, 3.5, 6.0,
          QStringLiteral("overwrite"), QStringLiteral("move"));
  if (!pancake_range_preview.value(QStringLiteral("available")).toBool() ||
      !pancake_range_preview.value(QStringLiteral("rangeMode")).toBool() ||
      pancake_range_preview.value(QStringLiteral("clipCount")).toInt() != 2 ||
      !nearly_equal(
          pancake_range_preview.value(QStringLiteral("durationSeconds")).toDouble(),
          2.5)) {
    return fail("pancake range preview should select every overlapping source track on exact 24 fps boundaries");
  }
  if (pancake_destination_controller.transferFromPancakeSequence(
          pancake_source_path, {}, 1.0, 3.5, 6.0,
          QStringLiteral("overwrite"), QStringLiteral("move")) != 2 ||
      pancake_destination_controller.clipCount() != 5) {
    return fail("pancake range move should transfer both overlapping tracks atomically");
  }
  pancake_destination_tracks = pancake_destination_controller.sequenceTracks();
  auto found_range_start = false;
  auto found_offset_range_start = false;
  for (auto track_index = 0; track_index < 2; ++track_index) {
    const auto clips = pancake_destination_tracks.at(track_index).toMap()
                           .value(QStringLiteral("clips")).toList();
    for (const auto& value : clips) {
      const auto clip = value.toMap();
      if (clip.value(QStringLiteral("displayLabel")).toString() ==
              QStringLiteral("Select A") &&
          nearly_equal(clip.value(QStringLiteral("startSeconds")).toDouble(), 6.0) &&
          nearly_equal(clip.value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) &&
          nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 2.5)) {
        found_range_start = true;
      }
      if (clip.value(QStringLiteral("displayLabel")).toString() ==
              QStringLiteral("Select B") &&
          nearly_equal(clip.value(QStringLiteral("startSeconds")).toDouble(), 7.0) &&
          nearly_equal(clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 1.5)) {
        found_offset_range_start = true;
      }
    }
  }
  if (!found_range_start || !found_offset_range_start) {
    return fail("range transfer should retain exact source trims and the one-second relative offset at the destination");
  }
  QFile pancake_source_after_move_file(pancake_source_path);
  if (!pancake_source_after_move_file.open(QIODevice::ReadOnly) ||
      pancake_source_after_move_file.readAll() == pancake_source_bytes_before_move) {
    return fail("Move should persist the source removal instead of behaving like a disguised copy");
  }
  pancake_destination_controller.undo();
  QFile pancake_source_after_undo_file(pancake_source_path);
  if (!pancake_source_after_undo_file.open(QIODevice::ReadOnly) ||
      pancake_source_after_undo_file.readAll() != pancake_source_bytes_before_move ||
      pancake_destination_controller.clipCount() != 3) {
    return fail("one ordinary Undo should restore both saved source bytes and the destination sequence");
  }
  pancake_destination_controller.redo();
  QFile pancake_source_after_redo_file(pancake_source_path);
  if (!pancake_source_after_redo_file.open(QIODevice::ReadOnly) ||
      pancake_source_after_redo_file.readAll() == pancake_source_bytes_before_move ||
      pancake_destination_controller.clipCount() != 5) {
    return fail("one ordinary Redo should reapply both sides of a cross-sequence move");
  }
  if (!pancake_destination_controller.saveTimeline()) {
    return fail("the destination should save after copy and move transfers");
  }
  TimelineDocumentController reopened_pancake_destination;
  TimelineDocumentController reopened_pancake_source;
  if (!reopened_pancake_destination.loadTimeline(
          pancake_destination_controller.savedDocumentPath()) ||
      reopened_pancake_destination.clipCount() != 5 ||
      !reopened_pancake_source.loadTimeline(pancake_source_path) ||
      reopened_pancake_source.clipCount() != 3) {
    return fail("both pancake sequences should reopen with their transferred and retained material intact");
  }

  TimelineDocumentController replace_controller;
  replace_controller.createTimeline(QStringLiteral("Replace Edit"));
  replace_controller.addClipFromGallery(
      QStringLiteral("video:replace-target"), first_video, QStringLiteral("Original Shot"), 3.0,
      QStringLiteral("poster://replace-target"));
  replace_controller.addSelectedClipEffect(QStringLiteral("Exposure"));
  replace_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.65);
  replace_controller.loadSourceClipFromGallery(
      QStringLiteral("video:replace-source"), second_video, QStringLiteral("Replacement Shot"), 5.0,
      QStringLiteral("poster://replace-source"));
  if (!wait_for_media_analysis(&replace_controller)) {
    return fail("replacement source analysis should finish in the background");
  }
  replace_controller.setSourceInSeconds(1.0);
  replace_controller.setSourceOutSeconds(5.0);
  if (!replace_controller.replaceSelectedClipFromSource()) {
    return fail("Replace from Source should accept a sufficient marked source range");
  }
  const auto replaced_clip = replace_controller.currentClip();
  const auto replaced_effects = replaced_clip.value(QStringLiteral("effectDetails")).toList();
  if (replaced_clip.value(QStringLiteral("assetId")).toString() != QStringLiteral("video:replace-source") ||
      replaced_clip.value(QStringLiteral("displayLabel")).toString() != QStringLiteral("Replacement Shot") ||
      !nearly_equal(replaced_clip.value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(replaced_clip.value(QStringLiteral("trimEndSeconds")).toDouble(), 4.0) ||
      !nearly_equal(replace_controller.totalDurationSecondsValue(), 3.0) ||
      replaced_effects.size() != 1 ||
      !nearly_equal(replaced_effects.first().toMap().value(QStringLiteral("baseAmount")).toDouble(), 0.65)) {
    return fail("Replace from Source should preserve sequence timing and the target clip's finishing stack");
  }
  replace_controller.undo();
  if (replace_controller.currentClip().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Original Shot") ||
      !nearly_equal(replace_controller.totalDurationSecondsValue(), 3.0)) {
    return fail("Replace from Source should restore the original media and timing in one undo");
  }
  if (!replace_controller.fitToFillSelectedClipFromSource()) {
    return fail("Fit to Fill should retime a valid marked Source range into the selected edit");
  }
  const auto fitted_clip = replace_controller.currentClip();
  const auto fitted_effects = fitted_clip.value(QStringLiteral("effectDetails")).toList();
  if (fitted_clip.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("video:replace-source") ||
      !nearly_equal(fitted_clip.value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(fitted_clip.value(QStringLiteral("trimEndSeconds")).toDouble(), 5.0) ||
      !nearly_equal(fitted_clip.value(QStringLiteral("playbackRate")).toDouble(), 4.0 / 3.0) ||
      !nearly_equal(replace_controller.totalDurationSecondsValue(), 3.0) ||
      fitted_effects.size() != 1 ||
      !nearly_equal(fitted_effects.first().toMap().value(QStringLiteral("baseAmount")).toDouble(),
                    0.65)) {
    return fail("Fit to Fill should use the complete Source marks and preserve duration and finishing");
  }
  replace_controller.undo();
  if (replace_controller.currentClip().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Original Shot")) {
    return fail("Fit to Fill should undo atomically to the original media");
  }
  replace_controller.redo();
  if (!nearly_equal(
          replace_controller.currentClip().value(QStringLiteral("playbackRate")).toDouble(),
          4.0 / 3.0) ||
      !replace_controller.saveTimeline()) {
    return fail("Fit to Fill should redo exactly and save its retimed replacement");
  }
  TimelineDocumentController reopened_fit_to_fill_controller;
  if (!reopened_fit_to_fill_controller.loadTimeline(replace_controller.savedDocumentPath()) ||
      reopened_fit_to_fill_controller.currentClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("video:replace-source") ||
      !nearly_equal(
          reopened_fit_to_fill_controller.currentClip()
              .value(QStringLiteral("playbackRate"))
              .toDouble(),
          4.0 / 3.0) ||
      !nearly_equal(reopened_fit_to_fill_controller.totalDurationSecondsValue(), 3.0)) {
    return fail("Fit to Fill should preserve source, speed, and edit duration after save and reopen");
  }

  TimelineDocumentController snap_controller;
  snap_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Magnetic Snap Timeline")},
      {QStringLiteral("frameRate"), 24.0},
  });
  snap_controller.addClipFromGallery(
      QStringLiteral("video:snap-one"), first_video, QStringLiteral("Snap One"), 4.0,
      QStringLiteral("poster://snap-one"));
  snap_controller.addClipFromGallery(
      QStringLiteral("video:snap-two"), second_video, QStringLiteral("Snap Two"), 3.0,
      QStringLiteral("poster://snap-two"));
  snap_controller.selectClip(0);
  snap_controller.assignSelectedClipTransition(QStringLiteral("out"), QStringLiteral("Fade"), 0.4);
  snap_controller.seekSequenceSeconds(2.5);
  const auto snap_marker_id = snap_controller.addSequenceMarker(QStringLiteral("Sync"));
  snap_controller.seekSequenceSeconds(1.75);

  auto snap_result = snap_controller.resolveTimelineSnap(1.79, 0.05);
  if (!snap_result.value(QStringLiteral("snapped")).toBool() ||
      snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("playhead") ||
      !nearly_equal(snap_result.value(QStringLiteral("seconds")).toDouble(), 1.75)) {
    return fail("magnetic snapping should acquire the playhead with a typed result");
  }
  snap_result = snap_controller.resolveTimelineSnap(4.02, 0.05);
  if (snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("video-edit") ||
      !nearly_equal(snap_result.value(QStringLiteral("seconds")).toDouble(), 4.0)) {
    return fail("magnetic snapping should acquire video edit points across the sequence");
  }
  snap_result = snap_controller.resolveTimelineSnap(3.61, 0.03);
  if (snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("transition") ||
      !nearly_equal(snap_result.value(QStringLiteral("seconds")).toDouble(), 3.6)) {
    return fail("magnetic snapping should expose transition boundaries as first-class targets");
  }
  snap_result = snap_controller.resolveTimelineSnap(2.51, 0.03);
  if (snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("marker") ||
      !snap_result.value(QStringLiteral("label")).toString().contains(QStringLiteral("Sync"))) {
    return fail("magnetic snapping should identify named marker targets in its feedback payload");
  }
  snap_result = snap_controller.resolveTimelineSnap(2.51, 0.03, QString{}, snap_marker_id);
  if (snap_result.value(QStringLiteral("kind")).toString() == QStringLiteral("marker")) {
    return fail("marker dragging should exclude the marker itself from magnetic target resolution");
  }
  snap_result = snap_controller.resolveTimelineSnap(1.021, 0.03);
  if (snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("frame") ||
      !nearly_equal(snap_result.value(QStringLiteral("seconds")).toDouble(), 25.0 / 24.0)) {
    return fail("magnetic snapping should remain frame-accurate at the sequence frame rate");
  }

  const auto waveform_cache_directory =
      QDir(AppBootstrap::cacheLocation()).filePath(QStringLiteral("timeline-waveforms"));
  QDir().mkpath(waveform_cache_directory);
  const QFileInfo waveform_source_info(audio_clip);
  const auto waveform_revision = QStringLiteral("%1:%2:%3")
                                     .arg(waveform_source_info.absoluteFilePath())
                                     .arg(waveform_source_info.size())
                                     .arg(waveform_source_info.lastModified().toMSecsSinceEpoch());
  const auto waveform_digest = QCryptographicHash::hash(
      QStringLiteral("timeline-waveform-v2:%1").arg(waveform_revision).toUtf8(),
      QCryptographicHash::Sha1).toHex();
  const auto waveform_cache_path = QDir(waveform_cache_directory).filePath(
      QStringLiteral("%1-standard.png").arg(QString::fromLatin1(waveform_digest)));
  QImage waveform_fixture(121, 41, QImage::Format_ARGB32);
  waveform_fixture.fill(qRgba(0, 0, 0, 255));
  for (const auto peak_x : {30, 90}) {
    const auto half_height = peak_x == 30 ? 14 : 18;
    for (auto x = peak_x - 1; x <= peak_x + 1; ++x) {
      for (auto y = 20 - half_height; y <= 20 + half_height; ++y) {
        waveform_fixture.setPixel(x, y, qRgba(235, 240, 245, 255));
      }
    }
  }
  if (!waveform_fixture.save(waveform_cache_path)) {
    return fail("the cached waveform peak fixture should be writable");
  }

  TimelineDocumentController audio_snap_controller;
  audio_snap_controller.createTimeline(QStringLiteral("Audio Magnetic Snap Timeline"));
  audio_snap_controller.addClipFromGallery(
      QStringLiteral("audio:snap"), audio_clip, QStringLiteral("Beat Track"), 4.0, QString{});
  snap_result = audio_snap_controller.resolveTimelineSnap(4.01, 0.03);
  if (snap_result.value(QStringLiteral("kind")).toString() != QStringLiteral("audio-edit") ||
      !nearly_equal(snap_result.value(QStringLiteral("seconds")).toDouble(), 4.0)) {
    return fail("magnetic snapping should include standalone audio edit points");
  }
  const auto frame_only_snap = audio_snap_controller.resolveTimelineSnap(1.001, 0.02);
  const auto audio_peak_snap =
      audio_snap_controller.resolveTimelineSnap(1.001, 0.02, QString{}, QString{}, true);
  if (frame_only_snap.value(QStringLiteral("kind")).toString() != QStringLiteral("frame") ||
      audio_peak_snap.value(QStringLiteral("kind")).toString() != QStringLiteral("audio-peak") ||
      !nearly_equal(audio_peak_snap.value(QStringLiteral("seconds")).toDouble(), 1.0, 0.01)) {
    return fail("optional audio-peak snapping should use strong cached waveform peaks without replacing frame snapping by default");
  }

  TimelineDocumentController photo_controller;
  photo_controller.createTimeline(QStringLiteral("Photo Timeline"));
  photo_controller.addClipFromGallery(
      QStringLiteral("photo:1"),
      photo_still,
      QStringLiteral("Still"),
      0.0,
      QStringLiteral("file:///poster.jpg"));
  if (photo_controller.clipCount() != 1 ||
      photo_controller.currentClip().value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo") ||
      !photo_controller.playbackSourceUrl().isEmpty()) {
    return fail("photo clips should be insertable onto video tracks without requiring video playback");
  }

  TimelineDocumentController audio_controller;
  audio_controller.createTimeline(QStringLiteral("Audio Timeline"));
  audio_controller.loadSourceClipFromGallery(
      QStringLiteral("audio:1"),
      audio_clip,
      QStringLiteral("Narration"),
      6.0,
      QString{});
  audio_controller.appendSourceClipToTimeline();
  const auto audio_tracks = audio_controller.sequenceTracks();
  if (audio_tracks.size() != 2 ||
      audio_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 0 ||
      audio_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
      audio_tracks.at(1).toMap().value(QStringLiteral("clips")).toList().at(0).toMap().value(QStringLiteral("mediaType")).toString()
          != QStringLiteral("audio")) {
    return fail("standalone audio clips should persist on audio lanes instead of being discarded during sync");
  }
  if (audio_controller.clips().size() != 1 || audio_controller.currentIndex() != 0 ||
      audio_controller.currentClip().value(QStringLiteral("mediaType")).toString() !=
          QStringLiteral("audio")) {
    return fail("standalone audio clips should participate in the selectable clip index and Inspector context");
  }

  TimelineDocumentController edit_controller;
  edit_controller.createTimeline(QStringLiteral("Edit Timeline"));
  edit_controller.addClipFromGallery(QStringLiteral("video:a"), first_video, QStringLiteral("Alpha"), 4.0, QStringLiteral("poster://a"));
  edit_controller.addClipFromGallery(QStringLiteral("video:b"), second_video, QStringLiteral("Beta"), 4.0, QStringLiteral("poster://b"));
  edit_controller.seekSequenceSeconds(3.0);
  edit_controller.addSequenceMarker(QStringLiteral("Insert Beat"));
  edit_controller.seekSequenceSeconds(1.5);
  edit_controller.insertClipFromGallery(
      QStringLiteral("video:insert"),
      first_video,
      QStringLiteral("Insert"),
      5.0,
      1.0,
      3.0,
      QStringLiteral("poster://insert"));
  if (edit_controller.clipCount() != 4) {
    return fail("insert edits should split the active clip and push later clips down the timeline");
  }
  const auto inserted_clips = edit_controller.clips();
  if (!nearly_equal(inserted_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.5) ||
      !nearly_equal(inserted_clips.at(2).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.5) ||
      !nearly_equal(inserted_clips.at(3).toMap().value(QStringLiteral("startSeconds")).toDouble(), 6.0)) {
    return fail("insert edits should preserve later clip order while shifting their sequence starts");
  }
  if (!nearly_equal(edit_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 5.0)) {
    return fail("insert edits should ripple later sequence markers forward with the inserted duration");
  }

  TimelineDocumentController lane_drop_controller;
  lane_drop_controller.createTimeline(QStringLiteral("Lane Drop Timeline"));
  lane_drop_controller.addVideoTrack();
  lane_drop_controller.insertClipFromGalleryAtTrack(
      QStringLiteral("video:lane-drop"),
      first_video,
      QStringLiteral("Lane Drop"),
      4.0,
      0.0,
      4.0,
      QStringLiteral("poster://lane-drop"),
      1,
      2.25);
  const auto lane_drop_tracks = lane_drop_controller.sequenceTracks();
  const auto lane_drop_clips = lane_drop_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
  if (lane_drop_clips.size() != 1 ||
      !nearly_equal(lane_drop_clips.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 2.25) ||
      lane_drop_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 0) {
    return fail("lane media drops should insert on the requested compatible track at the requested sequence time");
  }

  edit_controller.selectClip(0);
  edit_controller.seekSequenceSeconds(6.0);
  edit_controller.addSequenceMarker(QStringLiteral("Trim Beat"));
  edit_controller.selectClip(0);
  edit_controller.trimSelectedClipEndToSequenceSeconds(1.0, true);
  const auto ripple_clips = edit_controller.clips();
  if (!nearly_equal(ripple_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.0)) {
    return fail("ripple trims should close downstream timing after a trim-out change");
  }
  if (!nearly_equal(edit_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 4.5) ||
      !nearly_equal(edit_controller.sequenceMarkers().at(1).toMap().value(QStringLiteral("seconds")).toDouble(), 5.5)) {
    return fail("ripple trims should move later sequence markers with the rippled timeline");
  }
  edit_controller.selectClip(1);
  const auto slipped_before = edit_controller.currentClip();
  if (!edit_controller.slipSelectedClipBySeconds(0.5)) {
    return fail("slipping a selected clip should succeed when source handles remain available");
  }
  const auto slipped_after = edit_controller.currentClip();
  if (!nearly_equal(slipped_after.value(QStringLiteral("trimStartSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("trimStartSeconds")).toDouble() + 0.5) ||
      !nearly_equal(slipped_after.value(QStringLiteral("trimEndSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("trimEndSeconds")).toDouble() + 0.5) ||
      !nearly_equal(slipped_after.value(QStringLiteral("startSeconds")).toDouble(),
                    slipped_before.value(QStringLiteral("startSeconds")).toDouble())) {
    return fail("slipping should change source trim without moving the clip on the timeline");
  }

  TimelineDocumentController extract_controller;
  extract_controller.createTimeline(QStringLiteral("Extract Timeline"));
  extract_controller.addClipFromGallery(QStringLiteral("video:e1"), first_video, QStringLiteral("First"), 4.0, QStringLiteral("poster://e1"));
  extract_controller.addClipFromGallery(QStringLiteral("video:e2"), second_video, QStringLiteral("Second"), 4.0, QStringLiteral("poster://e2"));
  extract_controller.seekSequenceSeconds(5.0);
  const auto extract_marker_id = extract_controller.addSequenceMarker(QStringLiteral("After First"));
  extract_controller.selectClip(0);
  extract_controller.extractSelectedClip();
  if (extract_controller.clipCount() != 1 ||
      !nearly_equal(extract_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 1.0) ||
      extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("markerId")).toString() != extract_marker_id) {
    return fail("extracting a clip should close the gap and ripple later markers earlier");
  }
  extract_controller.selectClip(0);
  extract_controller.liftSelectedClip();
  if (extract_controller.clipCount() != 0) {
    return fail("lifting a clip should remove it without requiring ripple behavior");
  }

  TimelineDocumentController ripple_head_controller;
  ripple_head_controller.createTimeline(QStringLiteral("Ripple Head Timeline"));
  ripple_head_controller.addClipFromGallery(QStringLiteral("video:r1"), first_video, QStringLiteral("Head"), 4.0, QStringLiteral("poster://r1"));
  ripple_head_controller.addClipFromGallery(QStringLiteral("video:r2"), second_video, QStringLiteral("Tail"), 4.0, QStringLiteral("poster://r2"));
  ripple_head_controller.seekSequenceSeconds(5.0);
  ripple_head_controller.addSequenceMarker(QStringLiteral("After Head Trim"));
  ripple_head_controller.selectClip(0);
  ripple_head_controller.trimSelectedClipStartToSequenceSeconds(1.0, true);
  if (!nearly_equal(ripple_head_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(ripple_head_controller.currentClip().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0) ||
      !nearly_equal(ripple_head_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.0) ||
      !nearly_equal(ripple_head_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 4.0)) {
    return fail("ripple trims on the clip head should shorten the clip and pull later timeline items earlier");
  }

  TimelineDocumentController multi_extract_controller;
  multi_extract_controller.createTimeline(QStringLiteral("Multi Extract Timeline"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m1"), first_video, QStringLiteral("One"), 4.0, QStringLiteral("poster://m1"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m2"), second_video, QStringLiteral("Two"), 4.0, QStringLiteral("poster://m2"));
  multi_extract_controller.addClipFromGallery(QStringLiteral("video:m3"), first_video, QStringLiteral("Three"), 4.0, QStringLiteral("poster://m3"));
  multi_extract_controller.seekSequenceSeconds(10.0);
  multi_extract_controller.addSequenceMarker(QStringLiteral("After Multi"));
  multi_extract_controller.selectClipRange(0, 1);
  multi_extract_controller.extractSelectedClips();
  if (multi_extract_controller.clipCount() != 1 ||
      !nearly_equal(multi_extract_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
      !nearly_equal(multi_extract_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(), 2.0)) {
    return fail("extracting a multi-selection should close all removed ranges and ripple later markers");
  }

  TimelineDocumentController multi_lift_controller;
  multi_lift_controller.createTimeline(QStringLiteral("Multi Lift Timeline"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l1"), first_video, QStringLiteral("One"), 4.0, QStringLiteral("poster://l1"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l2"), second_video, QStringLiteral("Two"), 4.0, QStringLiteral("poster://l2"));
  multi_lift_controller.addClipFromGallery(QStringLiteral("video:l3"), first_video, QStringLiteral("Three"), 4.0, QStringLiteral("poster://l3"));
  multi_lift_controller.selectClipRange(0, 1);
  multi_lift_controller.liftSelectedClips();
  if (multi_lift_controller.clipCount() != 1 ||
      !nearly_equal(multi_lift_controller.clips().at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(), 8.0)) {
    return fail("lifting a multi-selection should remove the clips without closing the remaining gap");
  }

  TimelineDocumentController overwrite_controller;
  overwrite_controller.createTimeline(QStringLiteral("Overwrite Timeline"));
  overwrite_controller.addClipFromGallery(QStringLiteral("video:base-a"), first_video, QStringLiteral("Base A"), 4.0, QStringLiteral("poster://oa"));
  overwrite_controller.addClipFromGallery(QStringLiteral("video:base-b"), second_video, QStringLiteral("Base B"), 4.0, QStringLiteral("poster://ob"));
  overwrite_controller.seekSequenceSeconds(3.0);
  overwrite_controller.overwriteClipFromGallery(
      QStringLiteral("video:ovr"),
      first_video,
      QStringLiteral("Overwrite"),
      3.0,
      0.5,
      2.5,
      QStringLiteral("poster://ovr"));
  if (overwrite_controller.clipCount() != 3) {
    return fail("overwrite edits should trim away covered ranges while preserving uncovered material");
  }
  const auto overwritten_clips = overwrite_controller.clips();
  if (!nearly_equal(overwritten_clips.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble(), 3.0) ||
      !nearly_equal(overwritten_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 3.0) ||
      !nearly_equal(overwritten_clips.at(2).toMap().value(QStringLiteral("startSeconds")).toDouble(), 5.0) ||
      !nearly_equal(overwritten_clips.at(2).toMap().value(QStringLiteral("trimStartSeconds")).toDouble(), 1.0)) {
    return fail("overwrite edits should split overlapped clips at the in and out points");
  }

  overwrite_controller.selectClip(1);
  overwrite_controller.setSelectedFrameMode(QStringLiteral("fill"));
  overwrite_controller.setSelectedFrameScale(1.35);
  overwrite_controller.setSelectedFrameOffsetX(0.4);
  overwrite_controller.setSelectedFrameOffsetY(-0.25);
  overwrite_controller.setSelectedFrameRotationDegrees(17.5);
  overwrite_controller.setSelectedClipOpacity(0.62);
  overwrite_controller.setSelectedClipAudioGain(-4.5);
  overwrite_controller.setSelectedClipFadeInSeconds(0.35);
  overwrite_controller.setSelectedClipFadeOutSeconds(0.40);
  overwrite_controller.setSelectedClipPlaybackRate(1.25);
  overwrite_controller.addSelectedClipEffect(QStringLiteral("Transform"));
  overwrite_controller.addSelectedClipEffect(QStringLiteral("Opacity"));
  overwrite_controller.assignSelectedClipTransition(QStringLiteral("in"), QStringLiteral("Fade In"), 0.30);
  overwrite_controller.assignSelectedClipTransition(QStringLiteral("out"), QStringLiteral("Cross Dissolve"), 0.45);
  const auto overwrite_marker_id = overwrite_controller.addSequenceMarker(QStringLiteral("Start"));
  overwrite_controller.setSequenceMarkerNote(overwrite_marker_id, QStringLiteral("Lock this cut"));
  overwrite_controller.setSequenceMarkerStatus(overwrite_marker_id, QStringLiteral("approved"));
  const auto transformed_clip = overwrite_controller.currentClip();
  const auto transformed_effects = transformed_clip.value(QStringLiteral("effectStack")).toList();
  if (transformed_clip.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fill") ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameScale")).toDouble(), 1.35) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetX")).toDouble(), 0.4) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameOffsetY")).toDouble(), -0.25) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("frameRotationDegrees")).toDouble(), 17.5) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("opacity")).toDouble(), 0.62) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("audioGainDb")).toDouble(), -4.5) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("fadeInSeconds")).toDouble(), 0.35) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("fadeOutSeconds")).toDouble(), 0.40) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("playbackRate")).toDouble(), 1.25) ||
      transformed_effects.size() != 2 ||
      transformed_effects.at(0).toString() != QStringLiteral("Transform") ||
      transformed_effects.at(1).toString() != QStringLiteral("Opacity") ||
      transformed_clip.value(QStringLiteral("transitionInName")).toString() != QStringLiteral("Fade In") ||
      transformed_clip.value(QStringLiteral("transitionOutName")).toString() != QStringLiteral("Cross Dissolve") ||
      !nearly_equal(transformed_clip.value(QStringLiteral("transitionInSeconds")).toDouble(), 0.30) ||
      !nearly_equal(transformed_clip.value(QStringLiteral("transitionOutSeconds")).toDouble(), 0.45) ||
      overwrite_controller.sequenceMarkers().size() != 1) {
    return fail("extended clip controls, effects, transitions, and markers should persist on the selected clip");
  }
  if (overwrite_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("status")).toString() != QStringLiteral("approved")) {
    return fail("marker status should update independently of clip controls");
  }

  TimelineDocumentController rate_truth_controller;
  rate_truth_controller.createTimeline(QStringLiteral("Playback Rate Truth"));
  rate_truth_controller.addClipFromGallery(
      QStringLiteral("video:rate-one"), first_video, QStringLiteral("Rate One"), 4.0,
      QStringLiteral("poster://rate-one"));
  rate_truth_controller.addClipFromGallery(
      QStringLiteral("video:rate-two"), second_video, QStringLiteral("Rate Two"), 4.0,
      QStringLiteral("poster://rate-two"));
  rate_truth_controller.seekSequenceSeconds(7.0);
  rate_truth_controller.addSequenceMarker(QStringLiteral("Downstream"));
  rate_truth_controller.selectClip(0);
  rate_truth_controller.setSelectedClipPlaybackRate(2.0);
  if (!nearly_equal(
          rate_truth_controller.currentClip().value(QStringLiteral("trimmedDurationSeconds")).toDouble(),
          2.0) ||
      !nearly_equal(
          rate_truth_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          2.0) ||
      !nearly_equal(rate_truth_controller.totalDurationSecondsValue(), 6.0) ||
      !nearly_equal(
          rate_truth_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(),
          5.0)) {
    return fail("speed changes should ripple sequence duration, downstream clips, and markers together");
  }
  rate_truth_controller.undo();
  if (!nearly_equal(rate_truth_controller.totalDurationSecondsValue(), 8.0) ||
      !nearly_equal(
          rate_truth_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          4.0) ||
      !nearly_equal(
          rate_truth_controller.sequenceMarkers().at(0).toMap().value(QStringLiteral("seconds")).toDouble(),
          7.0)) {
    return fail("speed-change ripple timing should undo as one document edit");
  }
  rate_truth_controller.redo();
  rate_truth_controller.seekSequenceSeconds(1.5);
  if (!nearly_equal(rate_truth_controller.currentClipPlayheadSeconds(), 3.0) ||
      !rate_truth_controller.splitSelectedClip(2.0)) {
    return fail("speed-aware source mapping and splitting should preserve exact edit timing");
  }
  const auto rate_split_clips = rate_truth_controller.clips();
  if (rate_split_clips.size() != 3 ||
      !nearly_equal(rate_split_clips.at(0).toMap().value(QStringLiteral("endSeconds")).toDouble(), 1.0) ||
      !nearly_equal(rate_split_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 1.0) ||
      !nearly_equal(rate_split_clips.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble(), 2.0)) {
    return fail("splitting a retimed clip should preserve its source halves and sequence boundary");
  }

  TimelineDocumentController advanced_retime_controller;
  advanced_retime_controller.createTimeline(QStringLiteral("Advanced Retime Truth"));
  advanced_retime_controller.addClipFromGallery(
      QStringLiteral("video:advanced-retime-one"), first_video,
      QStringLiteral("Retime One"), 4.0, QStringLiteral("poster://retime-one"));
  advanced_retime_controller.addClipFromGallery(
      QStringLiteral("video:advanced-retime-two"), second_video,
      QStringLiteral("Retime Two"), 4.0, QStringLiteral("poster://retime-two"));
  advanced_retime_controller.selectClip(0);
  if (!advanced_retime_controller.applySelectedClipRetimePreset(QStringLiteral("ramp-up"))) {
    return fail("a selected video clip should accept the speed-ramp preset");
  }
  advanced_retime_controller.seekSequenceSeconds(1.0);
  auto advanced_retime_clip = advanced_retime_controller.currentClip();
  if (advanced_retime_clip.value(QStringLiteral("retimePoints")).toList().size() != 4 ||
      !advanced_retime_clip.value(QStringLiteral("retimeActive")).toBool() ||
      !nearly_equal(advanced_retime_clip.value(QStringLiteral("effectivePlaybackRate")).toDouble(), 0.5) ||
      !nearly_equal(advanced_retime_controller.currentClipPlayheadSeconds(), 0.5) ||
      !nearly_equal(advanced_retime_controller.totalDurationSecondsValue(), 8.0)) {
    return fail("speed ramps should expose their curve and share source, duration, and effective-rate truth");
  }
  if (!advanced_retime_controller.applySelectedClipRetimePreset(QStringLiteral("reverse"))) {
    return fail("a selected video clip should accept reverse retiming");
  }
  advanced_retime_controller.seekSequenceSeconds(1.0);
  advanced_retime_clip = advanced_retime_controller.currentClip();
  if (advanced_retime_clip.value(QStringLiteral("retimeDirection")).toString() !=
          QStringLiteral("reverse") ||
      !advanced_retime_clip.value(QStringLiteral("retimeAudioMuted")).toBool() ||
      !nearly_equal(advanced_retime_clip.value(QStringLiteral("effectivePlaybackRate")).toDouble(), -1.0) ||
      !nearly_equal(advanced_retime_controller.currentClipPlayheadSeconds(), 3.0)) {
    return fail("reverse retiming should map the sequence backward through source time and publish its audio policy");
  }
  advanced_retime_controller.seekSequenceSeconds(2.0);
  if (!advanced_retime_controller.applySelectedClipRetimePreset(QStringLiteral("freeze-frame")) ||
      !nearly_equal(advanced_retime_controller.totalDurationSecondsValue(), 9.0) ||
      !nearly_equal(
          advanced_retime_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          5.0)) {
    return fail("a freeze frame should add one second and ripple downstream clips as one edit");
  }
  advanced_retime_controller.seekSequenceSeconds(2.5);
  advanced_retime_clip = advanced_retime_controller.currentClip();
  if (advanced_retime_clip.value(QStringLiteral("retimeDirection")).toString() !=
          QStringLiteral("freeze") ||
      !advanced_retime_clip.value(QStringLiteral("retimeAudioMuted")).toBool() ||
      !nearly_equal(advanced_retime_controller.currentClipPlayheadSeconds(), 2.0)) {
    return fail("freeze sections should hold one exact source frame and mute their audio");
  }
  advanced_retime_controller.undo();
  if (!nearly_equal(advanced_retime_controller.totalDurationSecondsValue(), 8.0) ||
      !nearly_equal(
          advanced_retime_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          4.0)) {
    return fail("advanced retiming should undo its curve and downstream ripple atomically");
  }
  advanced_retime_controller.trimSelectedClipStartToSequenceSeconds(1.0, false);
  advanced_retime_controller.seekSequenceSeconds(1.0);
  if (!nearly_equal(
          advanced_retime_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(),
          1.0) ||
      !nearly_equal(
          advanced_retime_controller.currentClip().value(QStringLiteral("trimmedDurationSeconds")).toDouble(),
          3.0) ||
      !nearly_equal(advanced_retime_controller.currentClipPlayheadSeconds(), 3.0) ||
      !advanced_retime_controller.currentClip().value(QStringLiteral("retimeActive")).toBool()) {
    return fail("non-ripple edge trims should crop and rebase the source-time curve without changing its out point");
  }
  advanced_retime_controller.undo();
  advanced_retime_controller.selectClip(0);
  advanced_retime_controller.trimSelectedClipEndToSequenceSeconds(3.0, true);
  if (!nearly_equal(advanced_retime_controller.totalDurationSecondsValue(), 7.0) ||
      !nearly_equal(
          advanced_retime_controller.clips().at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          3.0) ||
      !advanced_retime_controller.currentClip().value(QStringLiteral("retimeActive")).toBool()) {
    return fail("ripple edge trims should crop a retime curve and move downstream clips atomically");
  }
  advanced_retime_controller.undo();
  if (!advanced_retime_controller.applySelectedClipRetimePreset(QStringLiteral("ramp-down")) ||
      !advanced_retime_controller.saveTimeline()) {
    return fail("advanced retiming should persist with the timeline document");
  }
  TimelineDocumentController reopened_retime_controller;
  if (!reopened_retime_controller.loadTimeline(
          advanced_retime_controller.savedDocumentPath())) {
    return fail("a timeline containing a source-time curve should reopen");
  }
  reopened_retime_controller.selectClip(0);
  reopened_retime_controller.seekSequenceSeconds(0.125);
  if (reopened_retime_controller.currentClip()
          .value(QStringLiteral("retimePoints")).toList().size() != 4 ||
      !nearly_equal(
          reopened_retime_controller.currentClip()
              .value(QStringLiteral("effectivePlaybackRate")).toDouble(),
          2.0)) {
    return fail("reopened retime curves should preserve their segment rates and source mapping");
  }
  reopened_retime_controller.seekSequenceSeconds(0.5);
  if (!reopened_retime_controller.splitSelectedClip(
          reopened_retime_controller.currentClipPlayheadSeconds())) {
    return fail("the razor should split a source-time curve at its exact displayed frame");
  }
  const auto split_retime_clips = reopened_retime_controller.clips();
  if (split_retime_clips.size() != 3 ||
      !nearly_equal(
          split_retime_clips.at(0).toMap().value(QStringLiteral("trimmedDurationSeconds")).toDouble(),
          0.5) ||
      !nearly_equal(
          split_retime_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
          0.5) ||
      !split_retime_clips.at(0).toMap().value(QStringLiteral("retimeActive")).toBool() ||
      !split_retime_clips.at(1).toMap().value(QStringLiteral("retimeActive")).toBool() ||
      !nearly_equal(reopened_retime_controller.totalDurationSecondsValue(), 8.0)) {
    return fail("splitting a retime curve should preserve both source-time halves and total sequence duration");
  }
  reopened_retime_controller.undo();

  TimelineDocumentController professional_retime_controller;
  professional_retime_controller.createTimeline(QStringLiteral("Professional Retime Options"));
  professional_retime_controller.addClipFromGallery(
      QStringLiteral("video:professional-retime-one"), first_video,
      QStringLiteral("Professional Retime One"), 4.0,
      QStringLiteral("poster://professional-retime-one"));
  professional_retime_controller.addClipFromGallery(
      QStringLiteral("video:professional-retime-two"), second_video,
      QStringLiteral("Professional Retime Two"), 4.0,
      QStringLiteral("poster://professional-retime-two"));
  professional_retime_controller.selectClip(0);
  if (!professional_retime_controller.applySelectedClipRetimePreset(QStringLiteral("ramp-up")) ||
      !professional_retime_controller.setSelectedClipRetimeOptions(
          QStringLiteral("follow-speed"), QStringLiteral("optical-flow"))) {
    return fail("professional speed ramps should expose selectable pitch and frame interpolation policies");
  }
  auto professional_retime_clip = professional_retime_controller.currentClip();
  if (professional_retime_clip.value(QStringLiteral("retimePitchStrategy")).toString() !=
          QStringLiteral("follow-speed") ||
      professional_retime_clip.value(QStringLiteral("retimeInterpolationMode")).toString() !=
          QStringLiteral("optical-flow")) {
    return fail("Program state should publish the selected speed-ramp audio and frame policies");
  }
  professional_retime_controller.undo();
  professional_retime_clip = professional_retime_controller.currentClip();
  if (professional_retime_clip.value(QStringLiteral("retimePitchStrategy")).toString() !=
          QStringLiteral("preserve") ||
      professional_retime_clip.value(QStringLiteral("retimeInterpolationMode")).toString() !=
          QStringLiteral("frame-sampling")) {
    return fail("speed-ramp policy changes should undo as one document edit");
  }
  professional_retime_controller.redo();
  if (!professional_retime_controller.addSelectedClipRetimeHold(1.0, 1.0) ||
      !nearly_equal(professional_retime_controller.totalDurationSecondsValue(), 9.0) ||
      !nearly_equal(
          professional_retime_controller.clips().at(1).toMap()
              .value(QStringLiteral("startSeconds")).toDouble(),
          5.0)) {
    return fail("an authored hold should extend the clip and ripple downstream timing atomically");
  }
  professional_retime_controller.seekSequenceSeconds(1.5);
  professional_retime_clip = professional_retime_controller.currentClip();
  if (professional_retime_clip.value(QStringLiteral("retimeDirection")).toString() !=
          QStringLiteral("freeze") ||
      !professional_retime_clip.value(QStringLiteral("retimeAudioMuted")).toBool() ||
      professional_retime_clip.value(QStringLiteral("retimeSourceLimitWarnings"))
          .toStringList().isEmpty()) {
    return fail("authored holds should freeze one source frame, mute audio, and publish source-limit guidance");
  }
  if (!professional_retime_controller.setSelectedClipKeyframeBezierHandle(
          QStringLiteral("sourceTime"), 0.0, QStringLiteral("out"), 0.20, 0.45) ||
      !professional_retime_controller.setSelectedClipKeyframeBezierHandlesLinked(
          QStringLiteral("sourceTime"), 0.0, false)) {
    return fail("source-time ramps should expose weighted Bezier handles and linked or broken state");
  }
  professional_retime_controller.seekSequenceSeconds(0.5);
  professional_retime_clip = professional_retime_controller.currentClip();
  const auto professional_retime_points =
      professional_retime_clip.value(QStringLiteral("retimePoints")).toList();
  if (professional_retime_points.size() != 6 ||
      professional_retime_points.first().toMap()
              .value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("bezier") ||
      !professional_retime_points.first().toMap()
           .value(QStringLiteral("bezierOutCustom")).toBool() ||
      nearly_equal(professional_retime_controller.currentClipPlayheadSeconds(), 0.25, 0.02)) {
    return fail("Program source seeking and effective rate should evaluate the authored Bezier speed curve");
  }
  if (!professional_retime_controller.saveTimeline()) {
    return fail("professional speed-ramp options and handles should save with the timeline");
  }
  TimelineDocumentController reopened_professional_retime_controller;
  if (!reopened_professional_retime_controller.loadTimeline(
          professional_retime_controller.savedDocumentPath())) {
    return fail("a timeline with professional speed-ramp options should reopen");
  }
  reopened_professional_retime_controller.selectClip(0);
  reopened_professional_retime_controller.seekSequenceSeconds(1.5);
  const auto reopened_professional_retime_clip =
      reopened_professional_retime_controller.currentClip();
  if (reopened_professional_retime_clip
          .value(QStringLiteral("retimePitchStrategy")).toString() !=
          QStringLiteral("follow-speed") ||
      reopened_professional_retime_clip
          .value(QStringLiteral("retimeInterpolationMode")).toString() !=
          QStringLiteral("optical-flow") ||
      reopened_professional_retime_clip
          .value(QStringLiteral("retimeDirection")).toString() !=
          QStringLiteral("freeze") ||
      reopened_professional_retime_clip.value(QStringLiteral("retimePoints"))
              .toList().first().toMap().value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("bezier")) {
    return fail("save and reopen should preserve hold timing, Bezier handles, pitch, and interpolation policy");
  }

  const auto effect_preset_store_path = workspace.filePath(QStringLiteral("effect-stack-presets.json"));
  qputenv("PHOTO_EDITOR_TIMELINE_EFFECT_PRESET_PATH", effect_preset_store_path.toUtf8());
  QFile legacy_effect_preset_file(effect_preset_store_path);
  const QJsonObject legacy_effect_preset_document{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("presets"),
       QJsonArray{QJsonObject{
           {QStringLiteral("name"), QStringLiteral("Legacy Creator")},
           {QStringLiteral("effects"),
            QJsonArray{
                QJsonObject{{QStringLiteral("effect"), QStringLiteral("Exposure")},
                            {QStringLiteral("value"), 0.22}},
                QStringLiteral("Missing Glow"),
            }},
       }}},
  };
  if (!legacy_effect_preset_file.open(QIODevice::WriteOnly) ||
      legacy_effect_preset_file.write(
          QJsonDocument(legacy_effect_preset_document).toJson(QJsonDocument::Compact)) <= 0) {
    return fail("effect-stack preset proof should write its legacy fixture");
  }
  legacy_effect_preset_file.close();

  TimelineDocumentController effects_controller;
  if (effects_controller.effectStackPresets().size() != 1 ||
      !effects_controller.effectStackPresetMigrationReport().contains(
          QStringLiteral("schema v2")) ||
      !effects_controller.effectStackPresetMigrationReport().contains(
          QStringLiteral("unavailable effect"))) {
    return fail("legacy effect presets should migrate to schema v2 while reporting retained missing effects");
  }
  const auto legacy_preset = effects_controller.effectStackPresets().constFirst().toMap();
  const auto legacy_preview = effects_controller.effectStackPresetPreview(
      legacy_preset.value(QStringLiteral("presetId")).toString());
  if (!legacy_preview.value(QStringLiteral("canApply")).toBool() ||
      legacy_preview.value(QStringLiteral("missingEffects")).toStringList() !=
          QStringList{QStringLiteral("Missing Glow")} ||
      legacy_preview.value(QStringLiteral("effects")).toList().size() != 2) {
    return fail("preset preview should retain exact supported parameters and identify missing effects without substitution");
  }
  effects_controller.createTimeline(QStringLiteral("Effects Timeline"));
  effects_controller.addClipFromGallery(
      QStringLiteral("video:effects"), first_video, QStringLiteral("Effects Source"), 4.0, QStringLiteral("poster://effects"));
  effects_controller.addSelectedClipEffect(QStringLiteral("exposure"));
  effects_controller.addSelectedClipEffect(QStringLiteral("Blur"));
  effects_controller.addSelectedClipEffect(QStringLiteral("Punch"));
  effects_controller.beginContinuousEdit();
  effects_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), -0.45);
  effects_controller.setSelectedClipEffectAmount(QStringLiteral("Punch"), 0.70);
  effects_controller.endContinuousEdit();
  effects_controller.setSelectedClipEffectEnabled(QStringLiteral("Blur"), false);
  effects_controller.setSelectedClipEffectsEnabled(false);

  const auto effect_detail_for = [](const QVariantMap& clip, const QString& name) {
    for (const auto& value : clip.value(QStringLiteral("effectDetails")).toList()) {
      const auto detail = value.toMap();
      if (detail.value(QStringLiteral("name")).toString() == name) {
        return detail;
      }
    }
    return QVariantMap{};
  };
  const auto effects_clip = effects_controller.currentClip();
  const auto exposure_detail = effect_detail_for(effects_clip, QStringLiteral("Exposure"));
  const auto blur_detail = effect_detail_for(effects_clip, QStringLiteral("Blur"));
  const auto punch_detail = effect_detail_for(effects_clip, QStringLiteral("Punch"));
  if (effects_clip.value(QStringLiteral("effectStack")).toList().size() != 3 ||
      effects_clip.value(QStringLiteral("effectsEnabled")).toBool() ||
      !nearly_equal(exposure_detail.value(QStringLiteral("amount")).toDouble(), -0.45) ||
      !exposure_detail.value(QStringLiteral("modified")).toBool() ||
      blur_detail.value(QStringLiteral("enabled")).toBool() ||
      !blur_detail.value(QStringLiteral("modified")).toBool() ||
      !nearly_equal(punch_detail.value(QStringLiteral("amount")).toDouble(), 0.70)) {
    return fail("effect stacks should expose canonical names, amounts, modified state, per-effect bypass, and stack bypass");
  }
  effects_controller.undo();
  if (!effects_controller.currentClip().value(QStringLiteral("effectsEnabled")).toBool()) {
    return fail("effect stack bypass should be undoable");
  }
  if (!effects_controller.moveSelectedClipEffect(QStringLiteral("Punch"), -1) ||
      effects_controller.currentClip().value(QStringLiteral("effectStack")).toList() !=
          QVariantList{QStringLiteral("Exposure"), QStringLiteral("Punch"), QStringLiteral("Blur")}) {
    return fail("applied effects should reorder through the controller");
  }
  effects_controller.undo();
  if (effects_controller.currentClip().value(QStringLiteral("effectStack")).toList() !=
      QVariantList{QStringLiteral("Exposure"), QStringLiteral("Blur"), QStringLiteral("Punch")}) {
    return fail("effect-stack reordering should restore through one undo");
  }
  effects_controller.clearSelectedClipEffects();
  if (!effects_controller.currentClip().value(QStringLiteral("effectStack")).toList().isEmpty()) {
    return fail("clearing an effect stack should remove every applied effect");
  }
  effects_controller.undo();
  if (effects_controller.currentClip().value(QStringLiteral("effectStack")).toList().size() != 3) {
    return fail("clearing an effect stack should restore through undo");
  }
  if (!effects_controller.saveTimeline()) {
    return fail("effect stack fixture should save");
  }
  TimelineDocumentController reopened_effects_controller;
  if (!reopened_effects_controller.loadTimeline(effects_controller.savedDocumentPath())) {
    return fail("saved effect stack fixture should reopen");
  }
  reopened_effects_controller.selectClip(0);
  const auto reopened_effects_clip = reopened_effects_controller.currentClip();
  if (!nearly_equal(
          effect_detail_for(reopened_effects_clip, QStringLiteral("Exposure"))
              .value(QStringLiteral("amount")).toDouble(),
          -0.45) ||
      effect_detail_for(reopened_effects_clip, QStringLiteral("Blur"))
          .value(QStringLiteral("enabled")).toBool()) {
    return fail("effect amount and bypass state should persist across save and reopen");
  }

  if (!effects_controller.saveSelectedClipEffectStackPreset(QStringLiteral("Creator Look"))) {
    return fail("the current effect stack should save as a reusable preset");
  }
  effects_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.20);
  effects_controller.setSelectedClipEffectEnabled(QStringLiteral("Blur"), true);
  if (!effects_controller.saveSelectedClipEffectStackPreset(QStringLiteral("Creator Look"))) {
    return fail("saving a matching preset name should retain a new revision");
  }
  QString first_preset_id;
  QString second_preset_id;
  for (const auto& preset_value : effects_controller.effectStackPresets()) {
    const auto preset = preset_value.toMap();
    if (preset.value(QStringLiteral("name")).toString() != QStringLiteral("Creator Look")) {
      continue;
    }
    if (preset.value(QStringLiteral("revision")).toInt() == 1) {
      first_preset_id = preset.value(QStringLiteral("presetId")).toString();
      if (preset.value(QStringLiteral("latest")).toBool()) {
        return fail("a superseded effect preset revision should remain available but stop being latest");
      }
    } else if (preset.value(QStringLiteral("revision")).toInt() == 2) {
      second_preset_id = preset.value(QStringLiteral("presetId")).toString();
      if (!preset.value(QStringLiteral("latest")).toBool()) {
        return fail("the newest effect preset revision should be identified explicitly");
      }
    }
  }
  const auto second_preset_preview = effects_controller.effectStackPresetPreview(second_preset_id);
  if (first_preset_id.isEmpty() || second_preset_id.isEmpty() ||
      second_preset_preview.value(QStringLiteral("revision")).toInt() != 2 ||
      !second_preset_preview.value(QStringLiteral("summary")).toString().contains(
          QStringLiteral("revision 2"))) {
    return fail("versioned presets should retain both revisions with a parameter-preview summary");
  }

  effects_controller.addClipFromGallery(
      QStringLiteral("video:preset-target"), second_video, QStringLiteral("Preset Target"), 4.0,
      QStringLiteral("poster://preset-target"));
  if (!effects_controller.applyEffectStackPreset(
          second_preset_id, QStringList{QStringLiteral("Exposure" )}, true)) {
    return fail("a preset should selectively apply compatible requested effects");
  }
  const auto selective_preset_clip = effects_controller.currentClip();
  if (selective_preset_clip.value(QStringLiteral("effectStack")).toList() !=
          QVariantList{QStringLiteral("Exposure")} ||
      !nearly_equal(
          effect_detail_for(selective_preset_clip, QStringLiteral("Exposure"))
              .value(QStringLiteral("amount")).toDouble(),
          0.20)) {
    return fail("selective preset application should use the previewed amount without adding unselected effects");
  }
  effects_controller.undo();
  if (!effects_controller.currentClip().value(QStringLiteral("effectStack")).toList().isEmpty()) {
    return fail("effect preset application should restore in one undo step");
  }
  effects_controller.redo();
  if (!effects_controller.saveTimeline()) {
    return fail("preset-applied effect state should save through the ordinary project path");
  }
  TimelineDocumentController reopened_preset_application;
  if (!reopened_preset_application.loadTimeline(effects_controller.savedDocumentPath())) {
    return fail("preset-applied effect state should reopen");
  }
  reopened_preset_application.selectClip(1);
  if (reopened_preset_application.currentClip().value(QStringLiteral("effectStack")).toList() !=
      QVariantList{QStringLiteral("Exposure")}) {
    return fail("preset-applied effect state should survive save and reopen");
  }
  TimelineDocumentController reusable_preset_reader;
  if (reusable_preset_reader.effectStackPresetPreview(second_preset_id)
          .value(QStringLiteral("revision")).toInt() != 2) {
    return fail("effect-stack presets should be reusable outside the project that authored them");
  }
  if (!effects_controller.deleteEffectStackPreset(second_preset_id)) {
    return fail("a saved preset revision should be deletable");
  }
  const auto promoted_preview = effects_controller.effectStackPresetPreview(first_preset_id);
  if (!promoted_preview.value(QStringLiteral("latest")).toBool()) {
    return fail("deleting the latest preset revision should promote the newest retained version");
  }

  overwrite_controller.selectClip(1);
  const auto roll_target = overwrite_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble() - 0.5;
  if (!overwrite_controller.rollSelectedClipStartToSequenceSeconds(roll_target)) {
    return fail("rolling the selected clip start should succeed when an adjacent left clip exists");
  }
  if (!nearly_equal(overwrite_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), roll_target)) {
    return fail("rolling a clip boundary should move the boundary to the requested sequence time");
  }

  TimelineDocumentController slide_controller;
  slide_controller.createTimeline(QStringLiteral("Slide Timeline"));
  slide_controller.addClipFromGallery(QStringLiteral("video:slide-a"), first_video, QStringLiteral("Left"), 6.0, QStringLiteral("poster://sa"));
  slide_controller.setSelectedTrimStartSeconds(0.5);
  slide_controller.setSelectedTrimEndSeconds(5.5);
  slide_controller.addClipFromGallery(QStringLiteral("video:slide-b"), second_video, QStringLiteral("Center"), 6.0, QStringLiteral("poster://sb"));
  slide_controller.setSelectedTrimStartSeconds(0.5);
  slide_controller.setSelectedTrimEndSeconds(5.5);
  slide_controller.addClipFromGallery(QStringLiteral("video:slide-c"), first_video, QStringLiteral("Right"), 6.0, QStringLiteral("poster://sc"));
  slide_controller.setSelectedTrimStartSeconds(0.5);
  slide_controller.setSelectedTrimEndSeconds(5.5);
  slide_controller.selectClip(1);
  if (!slide_controller.slideSelectedClipBySeconds(0.25)) {
    return fail("sliding a clip should succeed when both adjacent clips have usable source handles");
  }
  const auto slid_clips = slide_controller.clips();
  const auto slid_left = slid_clips.at(0).toMap();
  const auto slid_center = slid_clips.at(1).toMap();
  const auto slid_right = slid_clips.at(2).toMap();
  if (!nearly_equal(slid_left.value(QStringLiteral("trimEndSeconds")).toDouble(), 5.75) ||
      !nearly_equal(slid_center.value(QStringLiteral("startSeconds")).toDouble(), 5.25) ||
      !nearly_equal(slid_center.value(QStringLiteral("trimStartSeconds")).toDouble(), 0.5) ||
      !nearly_equal(slid_center.value(QStringLiteral("trimEndSeconds")).toDouble(), 5.5) ||
      !nearly_equal(slid_right.value(QStringLiteral("startSeconds")).toDouble(), 10.25) ||
      !nearly_equal(slid_right.value(QStringLiteral("trimStartSeconds")).toDouble(), 0.75) ||
      !nearly_equal(slide_controller.totalDurationSecondsValue(), 15.0)) {
    return fail("slide edits should move the selected clip, retime both neighbors, and preserve the outer sequence duration");
  }
  slide_controller.selectClip(0);
  if (slide_controller.slideSelectedClipBySeconds(0.1)) {
    return fail("sliding should require clips on both sides of the selected clip");
  }

  TimelineDocumentController inspector_undo_controller;
  inspector_undo_controller.createTimeline(QStringLiteral("Inspector Undo Timeline"));
  inspector_undo_controller.addClipFromGallery(
      QStringLiteral("video:undo"), first_video, QStringLiteral("Undo Clip"), 4.0, QStringLiteral("poster://undo"));
  inspector_undo_controller.setSelectedClipOpacity(0.4);
  if (!inspector_undo_controller.canUndo() ||
      !nearly_equal(inspector_undo_controller.currentClip().value(QStringLiteral("opacity")).toDouble(), 0.4)) {
    return fail("inspector property edits should create an undoable document change");
  }
  inspector_undo_controller.undo();
  if (!nearly_equal(inspector_undo_controller.currentClip().value(QStringLiteral("opacity")).toDouble(), 1.0)) {
    return fail("undo should restore the previous inspector property value");
  }
  inspector_undo_controller.redo();
  if (!nearly_equal(inspector_undo_controller.currentClip().value(QStringLiteral("opacity")).toDouble(), 0.4)) {
    return fail("redo should restore the inspector property edit");
  }
  inspector_undo_controller.beginContinuousEdit();
  inspector_undo_controller.setSelectedFrameScale(1.2);
  inspector_undo_controller.setSelectedFrameScale(1.7);
  inspector_undo_controller.endContinuousEdit();
  inspector_undo_controller.undo();
  if (!nearly_equal(inspector_undo_controller.currentClip().value(QStringLiteral("frameScale")).toDouble(), 1.0)) {
    return fail("continuous inspector drags should collapse into one undo step");
  }

  TimelineDocumentController keyframe_controller;
  keyframe_controller.createTimeline(QStringLiteral("Motion Keyframes"));
  keyframe_controller.addClipFromGallery(
      QStringLiteral("video:keyframes"),
      first_video,
      QStringLiteral("Animated Clip"),
      4.0,
      QStringLiteral("poster://keyframes"));
  keyframe_controller.seekSequenceSeconds(0.0);
  if (!keyframe_controller.toggleSelectedClipKeyframe(QStringLiteral("opacity"))) {
    return fail("the Inspector diamond should add a keyframe at the playhead");
  }
  keyframe_controller.setSelectedClipOpacity(0.2);
  keyframe_controller.seekSequenceSeconds(2.0);
  keyframe_controller.setSelectedClipOpacity(0.8);
  auto keyframed_clip = keyframe_controller.currentClip();
  if (keyframed_clip.value(QStringLiteral("keyframes")).toList().size() != 2 ||
      !keyframed_clip.value(QStringLiteral("keyframedParameters")).toStringList().contains(
          QStringLiteral("opacity"))) {
    return fail("animated Inspector edits should expose a persistent parameter track");
  }
  keyframe_controller.seekSequenceSeconds(1.0);
  if (!nearly_equal(
          keyframe_controller.currentClip().value(QStringLiteral("opacity")).toDouble(),
          0.5)) {
    return fail("linear keyframes should evaluate between neighboring values");
  }
  if (!keyframe_controller.setSelectedClipKeyframeInterpolation(
          QStringLiteral("opacity"), 0.0, QStringLiteral("hold"))) {
    return fail("keyframe interpolation should be editable");
  }
  if (!nearly_equal(
          keyframe_controller.currentClip().value(QStringLiteral("opacity")).toDouble(),
          0.2)) {
    return fail("hold interpolation should retain the outgoing value");
  }
  if (!keyframe_controller.moveSelectedClipKeyframe(QStringLiteral("opacity"), 2.0, 3.0)) {
    return fail("timeline keyframes should move within the selected clip");
  }
  keyframe_controller.seekSequenceSeconds(2.0);
  if (!nearly_equal(
          keyframe_controller.currentClip().value(QStringLiteral("opacity")).toDouble(),
          0.2)) {
    return fail("moving a hold keyframe should update the evaluated animation timing");
  }
  keyframe_controller.undo();
  keyframe_controller.seekSequenceSeconds(2.0);
  if (!nearly_equal(
          keyframe_controller.currentClip().value(QStringLiteral("opacity")).toDouble(),
          0.8)) {
    return fail("keyframe timing edits should be undoable atomically");
  }
  if (!keyframe_controller.setSelectedClipKeyframeBezierHandle(
          QStringLiteral("opacity"), 0.0, QStringLiteral("out"), 0.5, 0.6) ||
      !keyframe_controller.setSelectedClipKeyframeBezierHandlesLinked(
          QStringLiteral("opacity"), 0.0, false)) {
    return fail("Bezier keyframes should expose weighted time/value handles and linked or broken state");
  }
  keyframe_controller.seekSequenceSeconds(1.0);
  const auto bezier_clip_state = keyframe_controller.currentClip();
  const auto bezier_clip_points = bezier_clip_state.value(QStringLiteral("keyframes")).toList();
  if (bezier_clip_points.size() != 2 ||
      bezier_clip_points.first().toMap().value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("bezier") ||
      !bezier_clip_points.first().toMap().value(QStringLiteral("bezierOutCustom")).toBool() ||
      !nearly_equal(
          bezier_clip_points.first().toMap()
              .value(QStringLiteral("bezierOutTimeSeconds")).toDouble(),
          0.5) ||
      !nearly_equal(
          bezier_clip_points.first().toMap()
              .value(QStringLiteral("bezierOutValue")).toDouble(),
          0.6) ||
      bezier_clip_points.first().toMap()
          .value(QStringLiteral("bezierHandlesLinked")).toBool() ||
      bezier_clip_state.value(QStringLiteral("opacity")).toDouble() <= 0.55) {
    return fail("custom Bezier handles should publish and visibly reshape Program interpolation");
  }
  if (!keyframe_controller.saveTimeline()) {
    return fail("custom Bezier handle state should save");
  }
  TimelineDocumentController reopened_bezier_controller;
  if (!reopened_bezier_controller.loadTimeline(keyframe_controller.savedDocumentPath())) {
    return fail("custom Bezier handle state should reopen");
  }
  reopened_bezier_controller.seekSequenceSeconds(1.0);
  const auto reopened_bezier_points =
      reopened_bezier_controller.currentClip().value(QStringLiteral("keyframes")).toList();
  if (reopened_bezier_points.size() != 2 ||
      !reopened_bezier_points.first().toMap()
          .value(QStringLiteral("bezierOutCustom")).toBool() ||
      !nearly_equal(
          reopened_bezier_points.first().toMap()
              .value(QStringLiteral("bezierOutValue")).toDouble(),
          0.6) ||
      reopened_bezier_controller.currentClip()
              .value(QStringLiteral("opacity")).toDouble() <= 0.55) {
    return fail("custom Bezier handles should preserve their curve through save and reopen");
  }
  keyframe_controller.undo();
  keyframe_controller.undo();
  keyframe_controller.seekSequenceSeconds(2.0);
  if (keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList()
          .first().toMap().value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("hold")) {
    return fail("Bezier handle and linkage edits should each undo atomically");
  }

  TimelineDocumentController advanced_keyframe_controller;
  advanced_keyframe_controller.createTimeline(QStringLiteral("Advanced Keyframe Editing"));
  advanced_keyframe_controller.addClipFromGallery(
      QStringLiteral("video:advanced-keyframes"), first_video,
      QStringLiteral("Advanced Keyframe Clip"), 6.0,
      QStringLiteral("poster://advanced-keyframes"));
  const QVector<QPair<double, double>> advanced_opacity_points{
      {0.0, 0.10}, {1.0, 0.50}, {3.0, 0.90}, {5.0, 0.20}};
  for (const auto& [seconds, value] : advanced_opacity_points) {
    advanced_keyframe_controller.seekSequenceSeconds(seconds);
    if (seconds <= 1.0e-9 &&
        !advanced_keyframe_controller.toggleSelectedClipKeyframe(
            QStringLiteral("opacity"))) {
      return fail("the advanced curve fixture should create its first opacity keyframe");
    }
    advanced_keyframe_controller.setSelectedClipOpacity(value);
  }
  if (advanced_keyframe_controller.setSelectedClipKeyframesRoving(
          QStringLiteral("opacity"), QVariantList{1.0, 3.0}, true) != 2) {
    return fail("multiple interior keyframes should become roving in one edit");
  }
  auto advanced_keyframes = advanced_keyframe_controller.currentClip()
                                .value(QStringLiteral("keyframes")).toList();
  if (advanced_keyframes.size() != 4 ||
      !advanced_keyframes.at(1).toMap().value(QStringLiteral("roving")).toBool() ||
      !advanced_keyframes.at(2).toMap().value(QStringLiteral("roving")).toBool() ||
      !nearly_equal(
          advanced_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble(),
          4.0 / 3.0, 0.01) ||
      !nearly_equal(
          advanced_keyframes.at(2).toMap().value(QStringLiteral("seconds")).toDouble(),
          8.0 / 3.0, 0.01)) {
    return fail("roving timing should distribute between fixed anchors by cumulative value distance");
  }
  advanced_keyframe_controller.undo();
  advanced_keyframes = advanced_keyframe_controller.currentClip()
                           .value(QStringLiteral("keyframes")).toList();
  if (advanced_keyframes.at(1).toMap().value(QStringLiteral("roving")).toBool() ||
      !nearly_equal(
          advanced_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble(),
          1.0)) {
    return fail("one undo should restore fixed timing for a multi-key roving edit");
  }
  advanced_keyframe_controller.redo();
  advanced_keyframes = advanced_keyframe_controller.currentClip()
                           .value(QStringLiteral("keyframes")).toList();
  const auto first_roving_seconds =
      advanced_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble();
  if (!advanced_keyframe_controller.setSelectedClipKeyframeValue(
          QStringLiteral("opacity"), first_roving_seconds, 0.20)) {
    return fail("a roving keyframe value should remain directly editable");
  }
  advanced_keyframes = advanced_keyframe_controller.currentClip()
                           .value(QStringLiteral("keyframes")).toList();
  const auto reflowed_first_seconds =
      advanced_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble();
  const auto reflowed_second_seconds =
      advanced_keyframes.at(2).toMap().value(QStringLiteral("seconds")).toDouble();
  if (reflowed_first_seconds >= first_roving_seconds - 0.05 ||
      !advanced_keyframes.at(1).toMap().value(QStringLiteral("roving")).toBool()) {
    return fail("changing a roving value should immediately recompute its automatic timing");
  }
  if (!advanced_keyframe_controller.moveSelectedClipKeyframe(
          QStringLiteral("opacity"), reflowed_first_seconds, 0.75) ||
      advanced_keyframe_controller.currentClip().value(QStringLiteral("keyframes"))
          .toList().at(1).toMap().value(QStringLiteral("roving")).toBool()) {
    return fail("directly dragging a roving keyframe should intentionally pin its timing");
  }
  advanced_keyframe_controller.undo();
  if (advanced_keyframe_controller.copySelectedClipKeyframeSelection(
          QStringLiteral("opacity"),
          QVariantList{reflowed_first_seconds, reflowed_second_seconds}) != 2) {
    return fail("multi-key selection should copy only the selected curve points");
  }
  advanced_keyframe_controller.addClipFromGallery(
      QStringLiteral("video:advanced-keyframe-paste"), second_video,
      QStringLiteral("Advanced Keyframe Paste Target"), 6.0,
      QStringLiteral("poster://advanced-keyframe-paste"));
  if (advanced_keyframe_controller.pasteSelectedClipKeyframes(
          1.0, QStringLiteral("opacity")) != 2) {
    return fail("selected keyframes should paste at an exact destination playhead offset");
  }
  auto pasted_keyframes = advanced_keyframe_controller.currentClip()
                              .value(QStringLiteral("keyframes")).toList();
  if (pasted_keyframes.size() != 2 ||
      !nearly_equal(
          pasted_keyframes.first().toMap().value(QStringLiteral("seconds")).toDouble(),
          1.0, 0.01) ||
      !nearly_equal(
          pasted_keyframes.last().toMap().value(QStringLiteral("seconds")).toDouble(),
          1.0 + reflowed_second_seconds - reflowed_first_seconds, 0.01)) {
    return fail("paste-at-playhead should preserve selected relative timing without copying the full track");
  }
  advanced_keyframe_controller.selectClip(0);
  advanced_keyframes = advanced_keyframe_controller.currentClip()
                           .value(QStringLiteral("keyframes")).toList();
  QVariantList all_advanced_seconds;
  for (const auto& keyframe : advanced_keyframes) {
    all_advanced_seconds.push_back(
        keyframe.toMap().value(QStringLiteral("seconds")).toDouble());
  }
  if (advanced_keyframe_controller.alignSelectedClipKeyframes(
          QStringLiteral("opacity"), all_advanced_seconds,
          QStringLiteral("average")) != 4) {
    return fail("multi-key value alignment should apply as one command");
  }
  const auto aligned_keyframes = advanced_keyframe_controller.currentClip()
                                     .value(QStringLiteral("keyframes")).toList();
  for (const auto& keyframe : aligned_keyframes) {
    if (!nearly_equal(
            keyframe.toMap().value(QStringLiteral("value")).toDouble(),
            0.35, 0.01)) {
      return fail("average alignment should place every selected value on one exact line");
    }
  }
  advanced_keyframe_controller.undo();
  if (advanced_keyframe_controller.distributeSelectedClipKeyframes(
          QStringLiteral("opacity"), all_advanced_seconds,
          QStringLiteral("time")) != 4) {
    return fail("three or more selected keyframes should distribute evenly in time");
  }
  const auto distributed_keyframes = advanced_keyframe_controller.currentClip()
                                         .value(QStringLiteral("keyframes")).toList();
  if (!nearly_equal(
          distributed_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble(),
          5.0 / 3.0, 0.01) ||
      !nearly_equal(
          distributed_keyframes.at(2).toMap().value(QStringLiteral("seconds")).toDouble(),
          10.0 / 3.0, 0.01) ||
      distributed_keyframes.at(1).toMap().value(QStringLiteral("roving")).toBool()) {
    return fail("time distribution should create evenly spaced fixed keyframes");
  }
  const QVariantList middle_distributed_seconds{5.0 / 3.0, 10.0 / 3.0};
  if (advanced_keyframe_controller.scaleSelectedClipKeyframeSelection(
          QStringLiteral("opacity"), middle_distributed_seconds, 0.5, 2.0) != 2) {
    return fail("selected keyframes should scale in time and value around their selection center");
  }
  const auto scaled_keyframes = advanced_keyframe_controller.currentClip()
                                    .value(QStringLiteral("keyframes")).toList();
  if (!nearly_equal(
          scaled_keyframes.at(1).toMap().value(QStringLiteral("seconds")).toDouble(),
          2.083333, 0.01) ||
      !nearly_equal(
          scaled_keyframes.at(2).toMap().value(QStringLiteral("seconds")).toDouble(),
          2.916667, 0.01)) {
    return fail("selection scaling should preserve its center while changing its span");
  }
  advanced_keyframe_controller.undo();
  advanced_keyframe_controller.undo();
  advanced_keyframe_controller.setSelectedClipKeyframesRoving(
      QStringLiteral("opacity"),
      QVariantList{reflowed_first_seconds, reflowed_second_seconds}, true);
  if (!advanced_keyframe_controller.saveTimeline()) {
    return fail("advanced keyframe timing should save through the ordinary document path");
  }
  TimelineDocumentController reopened_advanced_keyframe_controller;
  if (!reopened_advanced_keyframe_controller.loadTimeline(
          advanced_keyframe_controller.savedDocumentPath())) {
    return fail("a document with advanced keyframe timing should reopen");
  }
  reopened_advanced_keyframe_controller.selectClip(0);
  const auto reopened_advanced_keyframes =
      reopened_advanced_keyframe_controller.currentClip()
          .value(QStringLiteral("keyframes")).toList();
  if (reopened_advanced_keyframes.size() != 4 ||
      !reopened_advanced_keyframes.at(1).toMap()
           .value(QStringLiteral("roving")).toBool() ||
      !reopened_advanced_keyframes.at(2).toMap()
           .value(QStringLiteral("roving")).toBool()) {
    return fail("roving identity and computed timing should survive save and reopen");
  }

  keyframe_controller.seekSequenceSeconds(0.0);
  if (!keyframe_controller.toggleTrackAutomationKeyframe(0, QStringLiteral("opacity")) ||
      !keyframe_controller.setTrackAutomationValue(0, QStringLiteral("opacity"), 0.4)) {
    return fail("video tracks should author opacity automation at the sequence playhead");
  }
  keyframe_controller.seekSequenceSeconds(2.0);
  if (!keyframe_controller.setTrackAutomationValue(0, QStringLiteral("opacity"), 0.8)) {
    return fail("editing an automated track should add a second sequence-level point");
  }
  keyframe_controller.seekSequenceSeconds(1.0);
  const auto automated_video_track = keyframe_controller.sequenceTracks().at(0).toMap();
  if (automated_video_track.value(QStringLiteral("automationKeyframes")).toList().size() != 2 ||
      !nearly_equal(automated_video_track.value(QStringLiteral("opacity")).toDouble(), 0.6)) {
    return fail("track opacity automation should evaluate continuously at the playhead");
  }
  if (!keyframe_controller.updateTrackAutomationKeyframe(
          0, QStringLiteral("opacity"), 2.0, 3.0, 0.9)) {
    return fail("an inline track lane should move an automation point in time and value atomically");
  }
  auto edited_track_points = keyframe_controller.sequenceTracks().at(0).toMap()
                                 .value(QStringLiteral("automationKeyframes")).toList();
  if (edited_track_points.size() != 2 ||
      !nearly_equal(edited_track_points.last().toMap().value(QStringLiteral("seconds")).toDouble(), 3.0) ||
      !nearly_equal(edited_track_points.last().toMap().value(QStringLiteral("value")).toDouble(), 0.9)) {
    return fail("direct track-lane edits should publish their new point geometry");
  }
  keyframe_controller.undo();
  if (!keyframe_controller.setTrackAutomationKeyframeInterpolation(
          0, QStringLiteral("opacity"), 0.0, QStringLiteral("hold"))) {
    return fail("track automation points should expose editable interpolation");
  }
  keyframe_controller.seekSequenceSeconds(1.0);
  if (!nearly_equal(
          keyframe_controller.sequenceTracks().at(0).toMap()
              .value(QStringLiteral("opacity")).toDouble(),
          0.4)) {
    return fail("track-lane hold interpolation should drive the evaluated Program value");
  }
  keyframe_controller.undo();
  const auto restored_interpolation_points =
      keyframe_controller.sequenceTracks().at(0).toMap()
          .value(QStringLiteral("automationKeyframes")).toList();
  if (restored_interpolation_points.size() != 2 ||
      restored_interpolation_points.first().toMap()
              .value(QStringLiteral("interpolation")).toString() != QStringLiteral("linear")) {
    return fail("undo should restore the prior track automation interpolation");
  }
  if (!keyframe_controller.removeTrackAutomationKeyframe(
          0, QStringLiteral("opacity"), 2.0) ||
      keyframe_controller.sequenceTracks().at(0).toMap()
              .value(QStringLiteral("automationKeyframes")).toList().size() != 1) {
    return fail("track-lane points should be directly removable");
  }
  keyframe_controller.undo();
  const auto restored_removed_points = keyframe_controller.sequenceTracks().at(0).toMap()
                                           .value(QStringLiteral("automationKeyframes")).toList();
  if (restored_removed_points.size() != 2 ||
      restored_removed_points.first().toMap()
              .value(QStringLiteral("interpolation")).toString() != QStringLiteral("linear")) {
    return fail("undo should restore a removed track point without changing its interpolation");
  }
  if (!keyframe_controller.setTrackAutomationValue(1, QStringLiteral("audioGainDb"), -6.0) ||
      !nearly_equal(
          keyframe_controller.sequenceTracks().at(1).toMap()
              .value(QStringLiteral("audioGainDb")).toDouble(),
          -6.0)) {
    return fail("audio tracks should expose an undoable track-gain stage");
  }
  if (!keyframe_controller.saveTimeline()) {
    return fail("keyframed timeline should save");
  }
  TimelineDocumentController reopened_keyframe_controller;
  if (!reopened_keyframe_controller.loadTimeline(keyframe_controller.savedDocumentPath())) {
    return fail("keyframed timeline should reopen");
  }
  reopened_keyframe_controller.seekSequenceSeconds(1.0);
  const auto reopened_keyframes =
      reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList();
  const auto reopened_video_track = reopened_keyframe_controller.sequenceTracks().at(0).toMap();
  const auto reopened_audio_track = reopened_keyframe_controller.sequenceTracks().at(1).toMap();
  if (reopened_keyframes.size() != 2 ||
      reopened_keyframes.first().toMap().value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("hold") ||
      !nearly_equal(
          reopened_keyframe_controller.currentClip().value(QStringLiteral("opacity")).toDouble(),
          0.2) ||
      reopened_video_track.value(QStringLiteral("automationKeyframes")).toList().size() != 2 ||
      !nearly_equal(reopened_video_track.value(QStringLiteral("opacity")).toDouble(), 0.6) ||
      !nearly_equal(reopened_audio_track.value(QStringLiteral("audioGainDb")).toDouble(), -6.0)) {
    std::cerr << "reopened keyframe state clip_points=" << reopened_keyframes.size()
              << " clip_interpolation="
              << reopened_keyframes.value(0).toMap()
                     .value(QStringLiteral("interpolation")).toString().toStdString()
              << " clip_opacity="
              << reopened_keyframe_controller.currentClip()
                     .value(QStringLiteral("opacity")).toDouble()
              << " track_points="
              << reopened_video_track.value(QStringLiteral("automationKeyframes")).toList().size()
              << " track_opacity="
              << reopened_video_track.value(QStringLiteral("opacity")).toDouble()
              << " audio_gain="
              << reopened_audio_track.value(QStringLiteral("audioGainDb")).toDouble()
              << '\n';
    return fail("clip and track automation should survive save and reopen");
  }
  if (reopened_keyframe_controller.copySelectedClipKeyframes(QStringLiteral("opacity")) != 2 ||
      reopened_keyframe_controller.keyframeClipboardCount() != 2) {
    return fail("parameter keyframes should copy into the reusable keyframe clipboard");
  }
  if (reopened_keyframe_controller.pasteSelectedClipKeyframes(1.0, QStringLiteral("opacity")) != 2 ||
      reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList().size() != 4) {
    return fail("copied keyframes should paste relative to the requested clip-local time");
  }
  if (!reopened_keyframe_controller.scaleSelectedClipKeyframeTimes(
          QStringLiteral("opacity"), 0.5)) {
    return fail("batch timing should compress a parameter track in one operation");
  }
  const auto compressed_keyframes =
      reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList();
  if (compressed_keyframes.size() != 4 ||
      !nearly_equal(compressed_keyframes.last().toMap().value(QStringLiteral("seconds")).toDouble(), 1.5)) {
    return fail("batch timing should scale every matching keyframe around the first point");
  }
  reopened_keyframe_controller.undo();
  reopened_keyframe_controller.undo();
  if (reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList().size() != 2) {
    return fail("keyframe paste and batch timing should each undo atomically");
  }
  if (!reopened_keyframe_controller.applySelectedClipMotionPreset(QStringLiteral("Social Pop"))) {
    return fail("the motion preset library should apply reusable production animation");
  }
  const auto preset_keyframes =
      reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList();
  auto preset_scale_keyframes = 0;
  for (const auto& value : preset_keyframes) {
    if (value.toMap().value(QStringLiteral("parameter")).toString() == QStringLiteral("frameScale")) {
      ++preset_scale_keyframes;
    }
  }
  if (preset_scale_keyframes != 3) {
    return fail("Social Pop should author a three-point scale animation without deleting other tracks");
  }
  reopened_keyframe_controller.undo();
  if (reopened_keyframe_controller.currentClip().value(QStringLiteral("keyframes")).toList().size() != 2) {
    return fail("applying a motion preset should be a single undoable document change");
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (!ffmpeg.isEmpty() && !ffprobe.isEmpty()) {
    // The controller-focused assertions above intentionally save many tiny
    // documents without running a background renderer.  Do not make the
    // encoded-media proofs wait behind that unrelated backlog once the runner
    // starts; every render asserted below is enqueued after this boundary.
    for (const auto& pending_job : loadPersistedJobRecords()) {
      if (!removePersistedJobRecord(pending_job.job_id)) {
        return fail("encoded-media fixture should clear unrelated pending render jobs");
      }
    }
    photo_editor::desktop::JobRunner runner;
    const auto render_video_one = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("render-01.mp4")),
        QStringLiteral("red"),
        true);
    const auto render_video_two = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("render-02.mp4")),
        QStringLiteral("blue"),
        false);
    const auto render_video_overlay = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("render-overlay.mp4")),
        QStringLiteral("blue"),
        true,
        990);
    const auto stereo_channel_video = create_test_video_with_channel_layout(
        ffmpeg,
        workspace.filePath(QStringLiteral("channel-map-stereo.mp4")),
        QStringLiteral("stereo"));
    const auto surround_channel_video = create_test_video_with_channel_layout(
        ffmpeg,
        workspace.filePath(QStringLiteral("channel-map-5-1.mp4")),
        QStringLiteral("5.1"));
    const auto timecode_video_one = create_test_timecode_video(
        ffmpeg, workspace.filePath(QStringLiteral("timecode-01.mov")),
        QStringLiteral("01:00:00:00"), QStringLiteral("red"));
    const auto timecode_video_two = create_test_timecode_video(
        ffmpeg, workspace.filePath(QStringLiteral("timecode-02.mov")),
        QStringLiteral("01:00:00:15"), QStringLiteral("blue"));
    const auto silence_analysis_video = create_test_video_with_silence(
        ffmpeg,
        workspace.filePath(QStringLiteral("silence-analysis.mp4")));
    const auto beat_analysis_video = create_test_video_with_beats(
        ffmpeg,
        workspace.filePath(QStringLiteral("beat-analysis.mp4")));
    const auto moving_subject_video = create_moving_subject_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("moving-subject.mp4")));
    const auto shaky_camera_video = create_shaky_camera_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("shaky-camera.mp4")));
    const auto rotation_image =
        create_rotation_test_image(workspace.filePath(QStringLiteral("rotation-source.png")));
    const auto position_image =
        create_position_test_image(workspace.filePath(QStringLiteral("position-source.png")));
    const auto alpha_delivery_image = create_alpha_delivery_test_image(
        workspace.filePath(QStringLiteral("alpha-delivery-source.png")));
    if (render_video_one.isEmpty() || render_video_two.isEmpty() || render_video_overlay.isEmpty() ||
        stereo_channel_video.isEmpty() || surround_channel_video.isEmpty() ||
        timecode_video_one.isEmpty() || timecode_video_two.isEmpty() ||
        silence_analysis_video.isEmpty() || beat_analysis_video.isEmpty() ||
        moving_subject_video.isEmpty() || shaky_camera_video.isEmpty() ||
        rotation_image.isEmpty() || position_image.isEmpty() || alpha_delivery_image.isEmpty()) {
      return fail("fixture videos should be created when ffmpeg is available");
    }
    const auto analysis_media =
        workspace.filePath(QStringLiteral("background-analysis-source.mp4"));
    if (!QFile::copy(render_video_one, analysis_media)) {
      return fail("media analysis fixture should have an isolated source revision");
    }
    const auto fit_source_media =
        workspace.filePath(QStringLiteral("fit-to-fill-source.mp4"));
    const auto fit_target_media =
        workspace.filePath(QStringLiteral("fit-to-fill-target.mp4"));
    if (!QFile::copy(render_video_one, fit_source_media) ||
        !QFile::copy(render_video_two, fit_target_media)) {
      return fail("Fit to Fill fixture should have isolated source revisions");
    }

    TimelineDocumentController encoded_fit_to_fill_controller;
    encoded_fit_to_fill_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Encoded Fit to Fill")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    encoded_fit_to_fill_controller.addClipFromGallery(
        QStringLiteral("video:fit-target"), fit_target_media,
        QStringLiteral("Blue Target"), 0.0, QString{});
    if (!wait_for_media_analysis(&encoded_fit_to_fill_controller)) {
      return fail("Fit to Fill target analysis should finish");
    }
    encoded_fit_to_fill_controller.setSelectedTrimStartSeconds(0.0);
    encoded_fit_to_fill_controller.setSelectedTrimEndSeconds(0.6);
    encoded_fit_to_fill_controller.loadSourceClipFromGallery(
        QStringLiteral("video:fit-source"), fit_source_media,
        QStringLiteral("Red Source"), 0.0, QString{});
    if (!wait_for_media_analysis(&encoded_fit_to_fill_controller)) {
      return fail("Fit to Fill Source analysis should finish");
    }
    encoded_fit_to_fill_controller.setSourceInSeconds(0.0);
    encoded_fit_to_fill_controller.setSourceOutSeconds(
        encoded_fit_to_fill_controller.sourceClip()
            .value(QStringLiteral("durationSeconds"))
            .toDouble());
    if (!encoded_fit_to_fill_controller.fitToFillSelectedClipFromSource()) {
      return fail("encoded Fit to Fill should accept a two-times Source-to-edit ratio");
    }
    encoded_fit_to_fill_controller.seekSequenceSeconds(0.3);
    const auto encoded_fit_program = encoded_fit_to_fill_controller.programClip();
    if (encoded_fit_program.value(QStringLiteral("assetId")).toString() !=
            QStringLiteral("video:fit-source") ||
        !nearly_equal(encoded_fit_program.value(QStringLiteral("effectivePlaybackRate")).toDouble(),
                      2.0, 0.05) ||
        !nearly_equal(encoded_fit_to_fill_controller.totalDurationSecondsValue(), 0.6, 0.03) ||
        !encoded_fit_to_fill_controller.saveTimeline()) {
      return fail("Fit to Fill Program state should expose the retimed Source and exact edit duration");
    }
    const auto encoded_fit_job_id = QStringLiteral("timeline-render-%1").arg(
        encoded_fit_to_fill_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(encoded_fit_job_id)) {
      return fail("Fit to Fill should render through the ordinary timeline export path");
    }
    QString encoded_fit_probe_error;
    const auto encoded_fit_duration_text = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
            QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
            encoded_fit_to_fill_controller.savedRenderPath(),
        },
        &encoded_fit_probe_error);
    bool encoded_fit_duration_ok = false;
    const auto encoded_fit_duration =
        encoded_fit_duration_text.trimmed().toDouble(&encoded_fit_duration_ok);
    const auto encoded_fit_pixel = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.30"),
            QStringLiteral("-i"), encoded_fit_to_fill_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (!encoded_fit_duration_ok || encoded_fit_duration < 0.55 ||
        encoded_fit_duration > 0.70 || encoded_fit_pixel.size() < 3 ||
        static_cast<unsigned char>(encoded_fit_pixel.at(0)) < 150 ||
        static_cast<unsigned char>(encoded_fit_pixel.at(2)) > 90) {
      return fail("Fit to Fill export should preserve the target duration and render the replacement Source");
    }

    TimelineDocumentController media_analysis_controller;
    media_analysis_controller.createTimeline(QStringLiteral("Background Media Analysis"));
    QElapsedTimer media_add_timer;
    media_add_timer.start();
    media_analysis_controller.addClipFromGallery(
        QStringLiteral("video:background-analysis"),
        analysis_media,
        QStringLiteral("Analyze Without Blocking"),
        0.0,
        QString{});
    const auto provisional_clip = media_analysis_controller.currentClip();
    if (media_add_timer.elapsed() > 100 || !media_analysis_controller.mediaAnalysisBusy() ||
        media_analysis_controller.mediaAnalysisPendingCount() < 1 ||
        !provisional_clip.value(QStringLiteral("durationProvisional")).toBool() ||
        provisional_clip.value(QStringLiteral("mediaAnalysisStatus")).toString() ==
            QStringLiteral("ready") ||
        !provisional_clip.value(QStringLiteral("posterSource")).toString().isEmpty()) {
      return fail("timeline ingest should publish a provisional clip immediately without waiting for media probes");
    }
    if (!wait_for_media_analysis(&media_analysis_controller) ||
        media_analysis_controller.mediaAnalysisPendingCount() != 0) {
      return fail("the bounded metadata and poster queues should drain in the background");
    }
    const auto analyzed_clip = media_analysis_controller.currentClip();
    const auto analyzed_poster = analyzed_clip.value(QStringLiteral("posterSource")).toString();
    const auto analyzed_tracks = media_analysis_controller.sequenceTracks();
    if (analyzed_clip.value(QStringLiteral("durationProvisional")).toBool() ||
        analyzed_clip.value(QStringLiteral("mediaAnalysisStatus")).toString() !=
            QStringLiteral("ready") ||
        !nearly_equal(analyzed_clip.value(QStringLiteral("durationSeconds")).toDouble(), 1.2, 0.08) ||
        !analyzed_clip.value(QStringLiteral("hasAudio")).toBool() ||
        analyzed_clip.value(QStringLiteral("sourceWidth")).toInt() != 320 ||
        analyzed_clip.value(QStringLiteral("sourceHeight")).toInt() != 240 ||
        analyzed_poster.isEmpty() ||
        !QFileInfo(QUrl(analyzed_poster).toLocalFile()).exists() ||
        analyzed_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
      return fail("background analysis should resolve exact duration, audio, size, poster, and linked sound");
    }

    TimelineDocumentController channel_mapping_controller;
    channel_mapping_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Channel Mapping Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    channel_mapping_controller.addClipFromGallery(
        QStringLiteral("video:channel-map-stereo"), stereo_channel_video,
        QStringLiteral("Asymmetric Stereo"), 0.0, QString{});
    if (!wait_for_media_analysis(&channel_mapping_controller)) {
      return fail("stereo channel-map source analysis should finish");
    }
    auto channel_clip = channel_mapping_controller.currentClip();
    if (channel_clip.value(QStringLiteral("sourceAudioChannels")).toInt() != 2 ||
        !channel_clip.value(QStringLiteral("sourceAudioChannelLayout"))
             .toString()
             .contains(QStringLiteral("stereo"), Qt::CaseInsensitive) ||
        channel_clip.value(QStringLiteral("audioChannelMode")).toString() !=
            QStringLiteral("source")) {
      return fail("source analysis should publish exact stereo count, layout, and Source interpretation");
    }
    if (!channel_mapping_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("stereo"), true, true, false)) {
      return fail("a selected audio clip should accept stereo swap and left-polarity inversion atomically");
    }
    channel_clip = channel_mapping_controller.currentClip();
    if (channel_clip.value(QStringLiteral("audioChannelMode")).toString() !=
            QStringLiteral("stereo") ||
        !channel_clip.value(QStringLiteral("audioSwapChannels")).toBool() ||
        !channel_clip.value(QStringLiteral("audioInvertLeftPhase")).toBool() ||
        channel_clip.value(QStringLiteral("audioInvertRightPhase")).toBool()) {
      return fail("Program state should expose the complete clip-specific channel map");
    }
    channel_mapping_controller.undo();
    if (channel_mapping_controller.currentClip()
            .value(QStringLiteral("audioChannelMode"))
            .toString() != QStringLiteral("source") ||
        channel_mapping_controller.currentClip()
            .value(QStringLiteral("audioSwapChannels"))
            .toBool()) {
      return fail("one undo should restore the complete source-channel interpretation");
    }
    channel_mapping_controller.redo();
    if (!channel_mapping_controller.saveTimeline()) {
      return fail("channel mapping truth fixture should save");
    }
    TimelineDocumentController reopened_channel_mapping_controller;
    if (!reopened_channel_mapping_controller.loadTimeline(
            channel_mapping_controller.savedDocumentPath())) {
      return fail("saved channel mapping truth fixture should reopen");
    }
    channel_clip = reopened_channel_mapping_controller.currentClip();
    if (channel_clip.value(QStringLiteral("sourceAudioChannels")).toInt() != 2 ||
        channel_clip.value(QStringLiteral("audioChannelMode")).toString() !=
            QStringLiteral("stereo") ||
        !channel_clip.value(QStringLiteral("audioSwapChannels")).toBool() ||
        !channel_clip.value(QStringLiteral("audioInvertLeftPhase")).toBool()) {
      return fail("source metadata, stereo interpretation, swap, and polarity should survive reopen");
    }

    const auto swapped_channel_render =
        workspace.filePath(QStringLiteral("channel-map-swapped.mp4"));
    if (!reopened_channel_mapping_controller.renderCurrentTimeline(
            swapped_channel_render)) {
      return fail("swapped stereo channel mapping should render through delivery");
    }
    const auto swapped_left_db = detected_channel_mean_volume_db(
        ffmpeg, swapped_channel_render, 0);
    const auto swapped_right_db = detected_channel_mean_volume_db(
        ffmpeg, swapped_channel_render, 1);
    if (swapped_right_db < swapped_left_db + 12.0) {
      std::cerr << "swapped channel dB left=" << swapped_left_db
                << " right=" << swapped_right_db << '\n';
      return fail("delivery should swap asymmetric stereo channels exactly like Program");
    }

    if (!reopened_channel_mapping_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("mono-left"), false, false, false)) {
      return fail("the stereo source should accept mono-left interpretation");
    }
    const auto mono_left_render =
        workspace.filePath(QStringLiteral("channel-map-mono-left.mp4"));
    if (!reopened_channel_mapping_controller.renderCurrentTimeline(mono_left_render)) {
      return fail("mono-left interpretation should render through delivery");
    }
    const auto mono_left_l_db = detected_channel_mean_volume_db(
        ffmpeg, mono_left_render, 0);
    const auto mono_left_r_db = detected_channel_mean_volume_db(
        ffmpeg, mono_left_render, 1);
    if (qAbs(mono_left_l_db - mono_left_r_db) > 1.5) {
      return fail("mono-left delivery should duplicate source channel one to both output channels");
    }

    if (!reopened_channel_mapping_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("mono-right"), false, false, false)) {
      return fail("the stereo source should accept mono-right interpretation");
    }
    const auto mono_right_render =
        workspace.filePath(QStringLiteral("channel-map-mono-right.mp4"));
    if (!reopened_channel_mapping_controller.renderCurrentTimeline(mono_right_render)) {
      return fail("mono-right interpretation should render through delivery");
    }
    const auto mono_right_l_db = detected_channel_mean_volume_db(
        ffmpeg, mono_right_render, 0);
    const auto mono_right_r_db = detected_channel_mean_volume_db(
        ffmpeg, mono_right_render, 1);
    if (qAbs(mono_right_l_db - mono_right_r_db) > 1.5 ||
        mono_left_l_db < mono_right_l_db + 12.0) {
      std::cerr << "mono channel dB left-source=" << mono_left_l_db
                << " right-source=" << mono_right_l_db << '\n';
      return fail("mono-right delivery should duplicate only source channel two to both outputs");
    }
    if (!reopened_channel_mapping_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("dual-mono"), false, false, false) ||
        reopened_channel_mapping_controller.currentClip()
                .value(QStringLiteral("audioChannelMode"))
                .toString() != QStringLiteral("dual-mono") ||
        !reopened_channel_mapping_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("source"), false, false, false)) {
      return fail("dual-mono and Source interpretations should remain explicit, editable modes");
    }

    TimelineDocumentController downmix_controller;
    downmix_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Multichannel Downmix Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    downmix_controller.addClipFromGallery(
        QStringLiteral("video:channel-map-surround"), surround_channel_video,
        QStringLiteral("Six Channel Source"), 0.0, QString{});
    if (!wait_for_media_analysis(&downmix_controller) ||
        downmix_controller.currentClip()
                .value(QStringLiteral("sourceAudioChannels"))
                .toInt() != 6 ||
        !downmix_controller.setSelectedClipAudioChannelMapping(
            QStringLiteral("downmix-stereo"), false, false, false)) {
      return fail("six-channel source metadata should enable explicit stereo downmix");
    }
    const auto downmix_render =
        workspace.filePath(QStringLiteral("channel-map-downmix.mp4"));
    if (!downmix_controller.renderCurrentTimeline(downmix_render)) {
      return fail("multichannel downmix should render through delivery");
    }
    const auto downmix_channels = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-select_streams"), QStringLiteral("a:0"),
            QStringLiteral("-show_entries"), QStringLiteral("stream=channels"),
            QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
            downmix_render,
        });
    if (downmix_channels.toInt() != 2 ||
        detected_channel_mean_volume_db(ffmpeg, downmix_render, 0) < -60.0 ||
        detected_channel_mean_volume_db(ffmpeg, downmix_render, 1) < -60.0) {
      return fail("delivery should produce an audible standards-based stereo downmix from 5.1");
    }

    TimelineDocumentController cached_analysis_controller;
    cached_analysis_controller.createTimeline(QStringLiteral("Cached Media Analysis"));
    QElapsedTimer cached_add_timer;
    cached_add_timer.start();
    cached_analysis_controller.addClipFromGallery(
        QStringLiteral("video:cached-analysis"),
        analysis_media,
        QStringLiteral("Reuse Analysis"),
        0.0,
        QString{});
    const auto cached_clip = cached_analysis_controller.currentClip();
    if (cached_add_timer.elapsed() > 100 || cached_analysis_controller.mediaAnalysisBusy() ||
        cached_clip.value(QStringLiteral("durationProvisional")).toBool() ||
        cached_clip.value(QStringLiteral("mediaAnalysisStatus")).toString() !=
            QStringLiteral("ready") ||
        !nearly_equal(cached_clip.value(QStringLiteral("durationSeconds")).toDouble(), 1.2, 0.08) ||
        cached_clip.value(QStringLiteral("posterSource")).toString().isEmpty()) {
      return fail("a matching source revision should reuse metadata and poster caches immediately");
    }

    QFile revised_analysis_media(analysis_media);
    if (!revised_analysis_media.open(QIODevice::Append) ||
        revised_analysis_media.write("revision") <= 0) {
      return fail("media analysis revision fixture should update the source fingerprint");
    }
    revised_analysis_media.close();
    TimelineDocumentController revised_analysis_controller;
    revised_analysis_controller.createTimeline(QStringLiteral("Revised Media Analysis"));
    revised_analysis_controller.addClipFromGallery(
        QStringLiteral("video:revised-analysis"),
        analysis_media,
        QStringLiteral("Invalidate Analysis"),
        0.0,
        QString{});
    if (!revised_analysis_controller.mediaAnalysisBusy() ||
        !revised_analysis_controller.currentClip()
             .value(QStringLiteral("durationProvisional")).toBool() ||
        !wait_for_media_analysis(&revised_analysis_controller) ||
        revised_analysis_controller.currentClip()
                .value(QStringLiteral("mediaAnalysisStatus")).toString() !=
            QStringLiteral("ready")) {
      return fail("a changed source revision should bypass stale analysis and regenerate in the background");
    }

    const auto resumable_media =
        workspace.filePath(QStringLiteral("resumable-analysis-source.mp4"));
    if (!QFile::copy(render_video_two, resumable_media)) {
      return fail("resumable analysis fixture should have an isolated source");
    }
    QString resumable_document_path;
    {
      TimelineDocumentController pending_analysis_controller;
      pending_analysis_controller.createTimeline(QStringLiteral("Resumable Media Analysis"));
      pending_analysis_controller.addClipFromGallery(
          QStringLiteral("video:resumable-analysis"),
          resumable_media,
          QStringLiteral("Resume After Reopen"),
          0.0,
          QString{});
      if (!pending_analysis_controller.currentClip()
               .value(QStringLiteral("durationProvisional")).toBool() ||
          !pending_analysis_controller.saveTimeline()) {
        return fail("a provisional media clip should be saveable without waiting for analysis");
      }
      resumable_document_path = pending_analysis_controller.savedDocumentPath();
    }
    TimelineDocumentController resumed_analysis_controller;
    if (!resumed_analysis_controller.loadTimeline(resumable_document_path) ||
        !resumed_analysis_controller.currentClip()
             .value(QStringLiteral("durationProvisional")).toBool() ||
        !resumed_analysis_controller.mediaAnalysisBusy() ||
        !wait_for_media_analysis(&resumed_analysis_controller)) {
      return fail("reopening a provisional clip should resume media analysis without blocking load");
    }
    const auto resumed_clip = resumed_analysis_controller.currentClip();
    if (resumed_clip.value(QStringLiteral("durationProvisional")).toBool() ||
        resumed_clip.value(QStringLiteral("mediaAnalysisStatus")).toString() !=
            QStringLiteral("ready") ||
        !nearly_equal(resumed_clip.value(QStringLiteral("durationSeconds")).toDouble(), 1.2, 0.08) ||
        resumed_clip.value(QStringLiteral("hasAudio")).toBool() ||
        resumed_clip.value(QStringLiteral("posterSource")).toString().isEmpty()) {
      return fail("resumed analysis should replace provisional state with exact silent-video metadata and poster");
    }

    TimelineDocumentController proxy_controller;
    proxy_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Proxy Playback Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    proxy_controller.addClipFromGallery(
        QStringLiteral("video:proxy"),
        render_video_two,
        QStringLiteral("Proxy Source"),
        0.0,
        QString{});
    const auto intent_proxy_clip_id =
        proxy_controller.currentClip().value(QStringLiteral("clipId")).toString();
    proxy_controller.setBackgroundWorkIntent(
        true, true, {intent_proxy_clip_id});
    QElapsedTimer proxy_request_timer;
    proxy_request_timer.start();
    if (!proxy_controller.generateSelectedClipProxy() || proxy_request_timer.elapsed() > 500) {
      return fail("proxy generation should enqueue without blocking timeline interaction");
    }
    const auto latency_sensitive_priority =
        proxy_controller.backgroundWorkPriorityState();
    if (!latency_sensitive_priority.value(QStringLiteral("yielding")).toBool() ||
        !latency_sensitive_priority.value(QStringLiteral("latencySensitive")).toBool() ||
        latency_sensitive_priority.value(QStringLiteral("focusedClipId")).toString() !=
            intent_proxy_clip_id ||
        !latency_sensitive_priority.value(QStringLiteral("visibleClipIds")).toStringList().contains(
            intent_proxy_clip_id) ||
        proxy_controller.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
            QStringLiteral("queued")) {
      return fail("active playback or direct manipulation should keep proxy work queued while publishing its focused and visible intent");
    }
    const auto proxy_background_jobs = proxy_controller.timelineBackgroundJobs();
    auto proxy_job_visible = false;
    QString proxy_background_job_id;
    for (const auto& value : proxy_background_jobs) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobKind")).toString() != QStringLiteral("proxy")) {
        continue;
      }
      proxy_background_job_id = job.value(QStringLiteral("jobId")).toString();
      proxy_job_visible =
          job.value(QStringLiteral("origin")).toString() == QStringLiteral("timeline") &&
          job.value(QStringLiteral("stage")).toString().contains(QStringLiteral("proxy"), Qt::CaseInsensitive) &&
          job.value(QStringLiteral("priority")).toString() == QStringLiteral("deferred") &&
          job.value(QStringLiteral("deferredByUserIntent")).toBool() &&
          job.value(QStringLiteral("priorityReason")).toString().contains(
              QStringLiteral("active edit"), Qt::CaseInsensitive) &&
          job.value(QStringLiteral("canCancel")).toBool() &&
          job.value(QStringLiteral("affectedType")).toString() == QStringLiteral("clip") &&
          job.value(QStringLiteral("jumpLabel")).toString() == QStringLiteral("Show clip");
      break;
    }
    if (!proxy_job_visible || proxy_background_job_id.isEmpty() ||
        !proxy_controller.activateTimelineBackgroundJob(proxy_background_job_id)) {
      return fail("the Timeline background-job center should expose active proxy stage, controls, and clip routing");
    }
    proxy_controller.setBackgroundWorkIntent(
        true, false, {intent_proxy_clip_id});
    if (proxy_controller.backgroundWorkPriorityState()
            .value(QStringLiteral("yielding")).toBool()) {
      return fail("queued background work should resume automatically after the interaction ends");
    }
    QFile proxy_revision_source(render_video_two);
    if (!proxy_revision_source.open(QIODevice::ReadWrite) ||
        !proxy_revision_source.setFileTime(
            QDateTime::currentDateTimeUtc().addSecs(-5),
            QFileDevice::FileModificationTime)) {
      return fail("proxy stale-result proof should advance the source revision while encoding");
    }
    proxy_revision_source.close();
    if (!wait_for_proxy(&proxy_controller)) {
      return fail("the bounded background proxy worker should publish a ready proxy");
    }
    const auto proxy_stale_state = proxy_controller.staleResultState();
    if (proxy_stale_state.value(QStringLiteral("discardCount")).toInt() < 1 ||
        proxy_stale_state.value(QStringLiteral("lastWorkflow")).toString() !=
            QStringLiteral("proxy") ||
        proxy_stale_state.value(QStringLiteral("lastAffectedItemId")).toString() !=
            intent_proxy_clip_id) {
      return fail("a proxy completion from an older media revision must be discarded before the current revision is regenerated");
    }
    const auto proxy_clip = proxy_controller.currentClip();
    const auto proxy_path = proxy_clip.value(QStringLiteral("proxyPath")).toString();
    if (proxy_path.isEmpty() || proxy_path == render_video_two || !QFileInfo::exists(proxy_path) ||
        !proxy_clip.value(QStringLiteral("usingProxy")).toBool() ||
        proxy_controller.playbackSourceUrl() != QUrl::fromLocalFile(proxy_path).toString() ||
        proxy_clip.value(QStringLiteral("originalSourceUrl")).toString() !=
            QUrl::fromLocalFile(render_video_two).toString()) {
      return fail("ready proxies should drive preview while retaining the immutable original source");
    }
    const auto proxy_integrity_path = proxy_path + QStringLiteral(".integrity.json");
    if (!wait_for_readable_file(proxy_integrity_path) ||
        proxy_controller.timelineProjectHealth()
                .value(QStringLiteral("generatedIntegrityVerifiedCount")).toInt() < 1 ||
        proxy_controller.timelineProjectHealth()
                .value(QStringLiteral("integrityMismatchCount")).toInt() != 0) {
      return fail("published proxies should carry a full SHA-256 sidecar that Project Health verifies");
    }
    if (!proxy_controller.toggleSelectedClipProxy() ||
        proxy_controller.playbackSourceUrl() != QUrl::fromLocalFile(render_video_two).toString()) {
      return fail("proxy switching should immediately restore original playback media");
    }
    if (!proxy_controller.toggleSelectedClipProxy() || !proxy_controller.saveTimeline()) {
      return fail("proxy selection should be persistent and timeline saving should still succeed");
    }
    TimelineDocumentController reopened_proxy_controller;
    if (!reopened_proxy_controller.loadTimeline(proxy_controller.savedDocumentPath()) ||
        !reopened_proxy_controller.currentClip().value(QStringLiteral("usingProxy")).toBool() ||
        reopened_proxy_controller.playbackSourceUrl() != QUrl::fromLocalFile(proxy_path).toString()) {
      return fail("ready proxy attachments should survive save and reopen");
    }
    TimelineDocumentController cached_proxy_controller;
    cached_proxy_controller.createTimeline(QStringLiteral("Cached Proxy Timeline"));
    cached_proxy_controller.addClipFromGallery(
        QStringLiteral("video:proxy-cache"),
        render_video_two,
        QStringLiteral("Cached Proxy Source"),
        0.0,
        QString{});
    if (!cached_proxy_controller.generateSelectedClipProxy() ||
        cached_proxy_controller.currentClip().value(QStringLiteral("proxyPath")).toString() != proxy_path ||
        cached_proxy_controller.currentClip().value(QStringLiteral("proxyStatus")).toString() !=
            QStringLiteral("ready")) {
      return fail("a source-revision proxy cache should be reused synchronously by matching clips");
    }
    if (!cached_proxy_controller.saveTimeline()) {
      return fail("the shared-proxy cache fixture should save a second project reference");
    }
    const auto cache_category = [](const QVariantMap& summary, const QString& key) {
      for (const auto& value : summary.value(QStringLiteral("categories")).toList()) {
        const auto category = value.toMap();
        if (category.value(QStringLiteral("key")).toString() == key) {
          return category;
        }
      }
      return QVariantMap{};
    };
    const auto shared_proxy_summary = reopened_proxy_controller.projectCacheSummary();
    const auto shared_proxy_category =
        cache_category(shared_proxy_summary, QStringLiteral("proxy"));
    if (shared_proxy_summary.value(QStringLiteral("categories")).toList().size() != 6 ||
        shared_proxy_category.value(QStringLiteral("fileCount")).toInt() < 1 ||
        shared_proxy_category.value(QStringLiteral("sharedFileCount")).toInt() < 1 ||
        shared_proxy_category.value(QStringLiteral("canClean")).toBool()) {
      return fail("the project cache manager should expose all six categories and retain a proxy referenced by another saved project");
    }
    const auto shared_proxy_cleanup = reopened_proxy_controller.cleanupProjectCaches(
        {QStringLiteral("proxy")});
    if (!shared_proxy_cleanup.value(QStringLiteral("ok")).toBool() ||
        shared_proxy_cleanup.value(QStringLiteral("removedFileCount")).toInt() != 0 ||
        shared_proxy_cleanup.value(QStringLiteral("retainedSharedFileCount")).toInt() < 1 ||
        !QFileInfo::exists(proxy_path) ||
        !QFileInfo::exists(render_video_two)) {
      return fail("per-project cleanup should retain shared proxies and never touch their original source");
    }
    QFile broken_proxy(proxy_path);
    if (!broken_proxy.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fail("proxy fixture should be writable for original-only export proof");
    }
    broken_proxy.write("not valid media");
    broken_proxy.close();
    if (reopened_proxy_controller.timelineProjectHealth()
            .value(QStringLiteral("integrityMismatchCount")).toInt() < 1) {
      return fail("Project Health should detect a generated proxy changed after publication");
    }
    const auto original_only_render = workspace.filePath(QStringLiteral("proxy-original-export.mp4"));
    if (!reopened_proxy_controller.renderCurrentTimeline(original_only_render) ||
        !QFileInfo::exists(original_only_render)) {
      return fail("timeline export should use original media even when the active proxy is corrupt");
    }
    if (!reopened_proxy_controller.generateSelectedClipProxy() ||
        !wait_for_proxy(&reopened_proxy_controller) ||
        !wait_for_readable_file(proxy_integrity_path) ||
        reopened_proxy_controller.timelineProjectHealth()
                .value(QStringLiteral("integrityMismatchCount")).toInt() != 0) {
      return fail("regenerating a tampered proxy should atomically rebuild media and its checksum");
    }
    if (!reopened_proxy_controller.removeSelectedClipProxy() || QFileInfo::exists(proxy_path)) {
      return fail("removing a proxy should clear its attachment and cached media cleanly");
    }
    if (QFileInfo::exists(proxy_integrity_path)) {
      return fail("removing generated media should also remove its checksum sidecar");
    }

    TimelineDocumentController cache_controller;
    cache_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Range Cache Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    cache_controller.addClipFromGallery(
        QStringLiteral("photo:cache-one"),
        rotation_image,
        QStringLiteral("Cache One"),
        4.0,
        QString{});
    cache_controller.addClipFromGallery(
        QStringLiteral("photo:cache-two"),
        rotation_image,
        QStringLiteral("Cache Two"),
        4.0,
        QString{});

    TimelineDocumentController stale_cache_controller;
    stale_cache_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Stale Range Cache")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    stale_cache_controller.addClipFromGallery(
        QStringLiteral("photo:stale-cache"), rotation_image,
        QStringLiteral("Stale Cache"), 4.0, QString{});
    if (!stale_cache_controller.generateRenderCache()) {
      return fail("render-cache stale-result proof should start a real background render");
    }
    stale_cache_controller.addSelectedClipEffect(QStringLiteral("Exposure"));
    stale_cache_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.42);
    QElapsedTimer stale_cache_wait;
    stale_cache_wait.start();
    while (stale_cache_wait.elapsed() < 120000 &&
           stale_cache_controller.renderCacheBusy()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    const auto render_stale_state = stale_cache_controller.staleResultState();
    if (stale_cache_controller.renderCacheBusy() ||
        stale_cache_controller.renderCacheStatus() != QStringLiteral("stale") ||
        stale_cache_controller.renderCacheReadyRangeCount() != 0 ||
        render_stale_state.value(QStringLiteral("lastWorkflow")).toString() !=
            QStringLiteral("render-cache") ||
        render_stale_state.value(QStringLiteral("discardCount")).toInt() < 1) {
      return fail("a preview-cache render from an older sequence revision must be discarded without publishing a range");
    }
    if (!stale_cache_controller.generateRenderCache() ||
        !wait_for_render_cache(&stale_cache_controller) ||
        stale_cache_controller.renderCacheReadyRangeCount() != 1) {
      return fail("the current sequence revision should render normally after stale cache output is discarded");
    }
    if (cache_controller.renderCacheRangeCount() != 2 ||
        cache_controller.renderCacheReadyRangeCount() != 0) {
      return fail("an eight-second sequence should expose two missing four-second cache ranges");
    }
    QElapsedTimer cache_request_timer;
    cache_request_timer.start();
    if (!cache_controller.generateRenderCache() || cache_request_timer.elapsed() > 500 ||
        !cache_controller.renderCacheBusy()) {
      return fail("preview cache generation should start asynchronously without blocking editing");
    }
    auto render_cache_job_visible = false;
    for (const auto& value : cache_controller.timelineBackgroundJobs()) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobId")).toString() ==
          QStringLiteral("timeline:render-cache")) {
        render_cache_job_visible =
            job.value(QStringLiteral("status")).toString() == QStringLiteral("Running") &&
            job.value(QStringLiteral("canCancel")).toBool() &&
            job.value(QStringLiteral("priority")).toString() == QStringLiteral("normal") &&
            job.value(QStringLiteral("affectedType")).toString() == QStringLiteral("timeline");
      }
    }
    if (!render_cache_job_visible) {
      return fail("the Timeline background-job center should report Program cache stage, progress, priority, and cancellation");
    }
    if (!wait_for_render_cache(&cache_controller) ||
        cache_controller.renderCacheReadyRangeCount() != 2) {
      return fail("the bounded cache worker should publish every missing preview range");
    }
    const auto initial_cache_ranges = cache_controller.renderCacheRanges();
    const auto first_cache_url =
        initial_cache_ranges.at(0).toMap().value(QStringLiteral("sourceUrl")).toString();
    const auto second_cache_url =
        initial_cache_ranges.at(1).toMap().value(QStringLiteral("sourceUrl")).toString();
    cache_controller.seekSequenceSeconds(1.0);
    if (cache_controller.renderCachePlaybackSourceUrl() != first_cache_url) {
      return fail("cache playback should resolve the exact ready range under the playhead");
    }
    cache_controller.selectClip(1);
    cache_controller.addSelectedClipEffect(QStringLiteral("Exposure"));
    cache_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.35);
    const auto invalidated_ranges = cache_controller.renderCacheRanges();
    if (cache_controller.renderCacheReadyRangeCount() != 1 ||
        !invalidated_ranges.at(0).toMap().value(QStringLiteral("ready")).toBool() ||
        invalidated_ranges.at(1).toMap().value(QStringLiteral("ready")).toBool() ||
        invalidated_ranges.at(0).toMap().value(QStringLiteral("sourceUrl")).toString() !=
            first_cache_url) {
      return fail("editing the second clip should invalidate only its affected cache range");
    }
    cache_controller.seekSequenceSeconds(5.0);
    if (!cache_controller.renderCachePlaybackSourceUrl().isEmpty()) {
      return fail("an invalid cache range should fall back to live compositing immediately");
    }
    if (!cache_controller.generateRenderCache() || !wait_for_render_cache(&cache_controller) ||
        cache_controller.renderCacheReadyRangeCount() != 2) {
      return fail("regeneration should fill only the missing render-state signature");
    }
    const auto regenerated_ranges = cache_controller.renderCacheRanges();
    if (regenerated_ranges.at(0).toMap().value(QStringLiteral("sourceUrl")).toString() !=
            first_cache_url ||
        regenerated_ranges.at(1).toMap().value(QStringLiteral("sourceUrl")).toString() ==
            second_cache_url) {
      return fail("cache regeneration should preserve valid ranges and publish a new affected range");
    }
    const auto render_cache_summary = cache_controller.projectCacheSummary();
    const auto render_cache_category =
        cache_category(render_cache_summary, QStringLiteral("render-cache"));
    if (render_cache_category.value(QStringLiteral("fileCount")).toInt() != 3 ||
        render_cache_category.value(QStringLiteral("reclaimableFileCount")).toInt() != 3 ||
        !render_cache_category.value(QStringLiteral("canClean")).toBool()) {
      return fail("the project cache manager should count current and invalidated render ranges owned by this project");
    }
    const auto render_cache_cleanup = cache_controller.cleanupProjectCaches(
        {QStringLiteral("render-cache")});
    if (!render_cache_cleanup.value(QStringLiteral("ok")).toBool() ||
        render_cache_cleanup.value(QStringLiteral("removedFileCount")).toInt() != 3 ||
        render_cache_cleanup.value(QStringLiteral("removedBytes")).toLongLong() <= 0 ||
        cache_controller.renderCacheReadyRangeCount() != 0 ||
        !QFileInfo::exists(rotation_image)) {
      return fail("confirmed per-project cleanup should remove only exact generated render ranges and preserve sources");
    }
    if (!cache_controller.generateRenderCache() || !wait_for_render_cache(&cache_controller) ||
        cache_controller.renderCacheReadyRangeCount() != 2) {
      return fail("cleaned Program ranges should regenerate through the ordinary preview-cache path");
    }
    TimelineDocumentController separate_render_cache_controller;
    separate_render_cache_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Separate Range Cache Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    separate_render_cache_controller.addClipFromGallery(
        QStringLiteral("photo:separate-cache-one"), rotation_image,
        QStringLiteral("Cache One"), 4.0, QString{});
    separate_render_cache_controller.addClipFromGallery(
        QStringLiteral("photo:separate-cache-two"), rotation_image,
        QStringLiteral("Cache Two"), 4.0, QString{});
    const auto first_project_range = cache_controller.renderCacheRanges().at(0).toMap();
    const auto second_project_range =
        separate_render_cache_controller.renderCacheRanges().at(0).toMap();
    if (first_project_range.value(QStringLiteral("signature")).toString() ==
            second_project_range.value(QStringLiteral("signature")).toString() ||
        separate_render_cache_controller.renderCacheReadyRangeCount() != 0) {
      return fail("render-cache signatures should include project identity so cleanup cannot evict an identical edit in another project");
    }
    cache_controller.clearRenderCache();
    if (cache_controller.renderCacheReadyRangeCount() != 0 ||
        !cache_controller.renderCachePlaybackSourceUrl().isEmpty()) {
      return fail("clearing the preview cache should remove only generated range media");
    }
    if (!cache_controller.generateRenderCache() || !cache_controller.renderCacheBusy() ||
        cache_controller.cleanupProjectCaches({QStringLiteral("render-cache")})
            .value(QStringLiteral("skippedBusyCategories")).toStringList()
            .value(0) != QStringLiteral("render-cache") ||
        !cache_controller.renderCacheBusy() ||
        !cache_controller.controlTimelineBackgroundJob(
            QStringLiteral("timeline:render-cache"), QStringLiteral("cancel"))) {
      return fail("an active background cache render should be immediately cancellable");
    }
    QElapsedTimer cache_cancel_wait;
    cache_cancel_wait.start();
    while (cache_cancel_wait.elapsed() < 5000 && cache_controller.renderCacheBusy()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    if (cache_controller.renderCacheBusy() ||
        cache_controller.renderCacheStatus() != QStringLiteral("cancelled")) {
      return fail("cache cancellation should terminate the worker and publish a stable cancelled state");
    }
    cache_controller.clearRenderCache();

    const auto cache_manager_video = create_test_video(
        ffmpeg,
        workspace.filePath(QStringLiteral("cache-manager-source.mp4")),
        QStringLiteral("purple"),
        true,
        735);
    if (cache_manager_video.isEmpty()) {
      return fail("the cache-manager source fixture should encode");
    }
    TimelineDocumentController project_cache_controller;
    project_cache_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Project Cache Ownership")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    project_cache_controller.addClipFromGallery(
        QStringLiteral("video:cache-manager"), cache_manager_video,
        QStringLiteral("Cache Manager Source"), 0.0, QString{});
    if (!wait_for_media_analysis(&project_cache_controller) ||
        !wait_for_waveform(&project_cache_controller) ||
        !project_cache_controller.generateSelectedClipProxy() ||
        !wait_for_proxy(&project_cache_controller) ||
        !project_cache_controller.saveTimeline()) {
      return fail("the cache-manager fixture should generate and save thumbnail, waveform, proxy, and analysis artifacts");
    }
    const auto project_cache_document = project_cache_controller.savedDocumentPath();
    const auto owned_cache_summary = project_cache_controller.projectCacheSummary();
    for (const auto& category_key : {
             QStringLiteral("thumbnail"), QStringLiteral("waveform"),
             QStringLiteral("proxy"), QStringLiteral("analysis")}) {
      const auto category = cache_category(owned_cache_summary, category_key);
      if (category.value(QStringLiteral("fileCount")).toInt() < 1 ||
          category.value(QStringLiteral("reclaimableFileCount")).toInt() < 1 ||
          !category.value(QStringLiteral("canClean")).toBool()) {
        return fail("the cache manager should attribute generated source artifacts to their exact project categories");
      }
    }
    const auto source_size_before_cleanup = QFileInfo(cache_manager_video).size();
    const auto owned_cache_cleanup = project_cache_controller.cleanupProjectCaches({
        QStringLiteral("thumbnail"), QStringLiteral("waveform"),
        QStringLiteral("proxy"), QStringLiteral("analysis")});
    if (!owned_cache_cleanup.value(QStringLiteral("ok")).toBool() ||
        owned_cache_cleanup.value(QStringLiteral("removedFileCount")).toInt() < 6 ||
        !QFileInfo(project_cache_document).isReadable() ||
        !QFileInfo(cache_manager_video).isReadable() ||
        QFileInfo(cache_manager_video).size() != source_size_before_cleanup ||
        !project_cache_controller.currentClip()
             .value(QStringLiteral("proxyPath")).toString().isEmpty() ||
        !project_cache_controller.currentClip()
             .value(QStringLiteral("waveformSource")).toString().isEmpty() ||
        !project_cache_controller.currentClip()
             .value(QStringLiteral("posterSource")).toString().isEmpty() ||
        project_cache_controller.currentClip()
                .value(QStringLiteral("mediaAnalysisStatus")).toString() !=
            QStringLiteral("pending")) {
      return fail("project cleanup should clear only generated references while preserving source media and the saved document byte-for-byte");
    }
    if (!project_cache_controller.saveTimeline()) {
      return fail("cache cleanup state should remain safely saveable");
    }
    TimelineDocumentController rebuilt_project_cache_controller;
    if (!rebuilt_project_cache_controller.loadTimeline(project_cache_document) ||
        !wait_for_media_analysis(&rebuilt_project_cache_controller) ||
        !wait_for_waveform(&rebuilt_project_cache_controller) ||
        rebuilt_project_cache_controller.currentClip()
            .value(QStringLiteral("posterSource")).toString().isEmpty() ||
        rebuilt_project_cache_controller.currentClip()
            .value(QStringLiteral("waveformSource")).toString().isEmpty()) {
      return fail("cleaned thumbnails, waveforms, and media analysis should regenerate through ordinary reopen hydration");
    }
    if (!rebuilt_project_cache_controller.generateSelectedClipProxy() ||
        !wait_for_proxy(&rebuilt_project_cache_controller) ||
        !QFileInfo(rebuilt_project_cache_controller.currentClip()
                       .value(QStringLiteral("proxyPath")).toString()).isReadable()) {
      return fail("a cleaned proxy should regenerate from the protected original source");
    }

    const auto budget_resource = [](const QVariantMap& state,
                                    const QString& key) -> QVariantMap {
      const auto resources = state.value(QStringLiteral("resources")).toList();
      for (const auto& value : resources) {
        const auto resource = value.toMap();
        if (resource.value(QStringLiteral("key")).toString() == key) {
          return resource;
        }
      }
      return {};
    };
    rebuilt_project_cache_controller.setCachePressureLevelForTesting(
        QStringLiteral("normal"));
    const auto automatic_budget_state =
        rebuilt_project_cache_controller.cacheBudgetState();
    if (!automatic_budget_state.value(QStringLiteral("automatic")).toBool() ||
        automatic_budget_state.value(QStringLiteral("resources")).toList().size() != 4 ||
        budget_resource(automatic_budget_state, QStringLiteral("disk"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() <= 0 ||
        budget_resource(automatic_budget_state, QStringLiteral("cpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() <= 0 ||
        !budget_resource(automatic_budget_state, QStringLiteral("gpu"))
             .value(QStringLiteral("estimatedUsage")).toBool() ||
        budget_resource(automatic_budget_state, QStringLiteral("memory"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() <= 0) {
      return fail("automatic cache budgets should cover disk, decoded CPU frames, honestly estimated GPU surfaces, and Timeline working memory");
    }
    constexpr qint64 kMiB = 1024LL * 1024LL;
    constexpr qint64 kGiB = 1024LL * kMiB;
    const QVariantMap custom_cache_budgets{
        {QStringLiteral("automatic"), false},
        {QStringLiteral("diskBytes"), 2LL * kGiB},
        {QStringLiteral("cpuMemoryBytes"), 96LL * kMiB},
        {QStringLiteral("gpuMemoryBytes"), 128LL * kMiB},
        {QStringLiteral("workingMemoryBytes"), 160LL * kMiB},
    };
    if (!rebuilt_project_cache_controller.setProjectCacheBudgets(
            custom_cache_budgets)) {
      return fail("project-specific cache budgets should apply atomically");
    }
    auto custom_budget_state = rebuilt_project_cache_controller.cacheBudgetState();
    if (custom_budget_state.value(QStringLiteral("automatic")).toBool() ||
        budget_resource(custom_budget_state, QStringLiteral("disk"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 2LL * kGiB ||
        budget_resource(custom_budget_state, QStringLiteral("cpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 96LL * kMiB ||
        budget_resource(custom_budget_state, QStringLiteral("gpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 128LL * kMiB ||
        budget_resource(custom_budget_state, QStringLiteral("memory"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 160LL * kMiB ||
        decoded_image_cache_budget_bytes() != 96LL * kMiB) {
      return fail("custom cache budgets should become the exact normal-pressure limits for every managed resource");
    }
    rebuilt_project_cache_controller.undo();
    if (!rebuilt_project_cache_controller.cacheBudgetState()
             .value(QStringLiteral("automatic")).toBool()) {
      return fail("one undo should restore automatic cache budgeting");
    }
    rebuilt_project_cache_controller.redo();
    if (rebuilt_project_cache_controller.cacheBudgetState()
            .value(QStringLiteral("automatic")).toBool()) {
      return fail("one redo should restore the project-specific cache budgets");
    }

    clear_decoded_image_cache();
    insert_decoded_image_cache_entry(
        QStringLiteral("cache-budget-pressure-a"),
        QImage(2048, 2048, QImage::Format_RGBA8888));
    insert_decoded_image_cache_entry(
        QStringLiteral("cache-budget-pressure-b"),
        QImage(2048, 2048, QImage::Format_RGBA8888));
    if (decoded_image_cache_snapshot().bytes <= 0) {
      return fail("the pressure fixture should populate the real decoded-frame cache");
    }
    rebuilt_project_cache_controller.setCachePressureLevelForTesting(
        QStringLiteral("critical"));
    const auto critical_budget_state =
        rebuilt_project_cache_controller.cacheBudgetState();
    if (critical_budget_state.value(QStringLiteral("pressureLevel")).toString() !=
            QStringLiteral("critical") ||
        !critical_budget_state.value(QStringLiteral("backgroundWorkDeferred")).toBool() ||
        critical_budget_state.value(QStringLiteral("responseCount")).toInt() < 1 ||
        decoded_image_cache_snapshot().bytes != 0 ||
        budget_resource(critical_budget_state, QStringLiteral("cpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() >= 96LL * kMiB) {
      return fail("critical pressure should reduce effective limits, clear decoded frames, and defer background work");
    }
    rebuilt_project_cache_controller.setCachePressureLevelForTesting(
        QStringLiteral("normal"));
    if (rebuilt_project_cache_controller.cacheBudgetState()
            .value(QStringLiteral("backgroundWorkDeferred")).toBool() ||
        decoded_image_cache_budget_bytes() != 96LL * kMiB) {
      return fail("normal pressure should restore the configured limit and resume background work");
    }
    if (!rebuilt_project_cache_controller.saveTimeline()) {
      return fail("project cache budget overrides should save with the Timeline document");
    }
    TimelineDocumentController reopened_cache_budget_controller;
    reopened_cache_budget_controller.setCachePressureLevelForTesting(
        QStringLiteral("normal"));
    if (!reopened_cache_budget_controller.loadTimeline(project_cache_document)) {
      return fail("the cache-budget persistence fixture should reopen");
    }
    const auto reopened_budget_state =
        reopened_cache_budget_controller.cacheBudgetState();
    if (reopened_budget_state.value(QStringLiteral("automatic")).toBool() ||
        budget_resource(reopened_budget_state, QStringLiteral("disk"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 2LL * kGiB ||
        budget_resource(reopened_budget_state, QStringLiteral("cpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 96LL * kMiB ||
        budget_resource(reopened_budget_state, QStringLiteral("gpu"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 128LL * kMiB ||
        budget_resource(reopened_budget_state, QStringLiteral("memory"))
                .value(QStringLiteral("effectiveBytes")).toLongLong() != 160LL * kMiB) {
      return fail("all four project cache budget overrides should survive save and reopen exactly");
    }
    reopened_cache_budget_controller.setCachePressureLevelForTesting(QString{});
    clear_decoded_image_cache();
    set_decoded_image_cache_budget_bytes(512LL * kMiB);

    TimelineDocumentController compound_controller;
    compound_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Compound Parent")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    compound_controller.addClipFromGallery(
        QStringLiteral("video:compound-one"), render_video_two,
        QStringLiteral("Compound One"), 0.0, QString{});
    compound_controller.addClipFromGallery(
        QStringLiteral("video:compound-two"), render_video_two,
        QStringLiteral("Compound Two"), 0.0, QString{});
    compound_controller.selectAllClips();
    QElapsedTimer compound_request_timer;
    compound_request_timer.start();
    if (!compound_controller.createCompoundClipFromSelection(QStringLiteral("Interview Selects")) ||
        compound_request_timer.elapsed() > 500 || compound_controller.clipCount() != 1 ||
        !compound_controller.currentClip().value(QStringLiteral("isCompound")).toBool()) {
      return fail("compound creation should atomically replace a multi-selection without blocking");
    }
    const auto compound_document_path = compound_controller.currentClip()
                                            .value(QStringLiteral("compoundDocumentPath"))
                                            .toString();
    if (compound_document_path.isEmpty() || !QFileInfo::exists(compound_document_path) ||
        !wait_for_compound(&compound_controller)) {
      return fail("compound creation should retain an editable child document and publish its preview");
    }
    const auto original_compound_duration = compound_controller.currentClip()
                                                .value(QStringLiteral("trimmedDurationSeconds"))
                                                .toDouble();
    compound_controller.addClipFromGallery(
        QStringLiteral("video:after-compound"), render_video_two,
        QStringLiteral("After Compound"), 0.0, QString{});
    const auto downstream_start = compound_controller.currentClip()
                                      .value(QStringLiteral("startSeconds"))
                                      .toDouble();
    compound_controller.selectClip(0);
    if (!compound_controller.openSelectedCompoundClip() ||
        !compound_controller.insideCompound() ||
        compound_controller.compoundNavigationDepth() != 1 ||
        compound_controller.compoundBreadcrumb() != QStringLiteral("Interview Selects") ||
        compound_controller.clipCount() != 2 || compound_controller.canUndo()) {
      return fail("opening a compound should enter its intact child edit graph with navigation context");
    }
    compound_controller.selectAllClips();
    if (!compound_controller.createCompoundClipFromSelection(QStringLiteral("Nested Selects")) ||
        !wait_for_compound(&compound_controller) ||
        !compound_controller.openSelectedCompoundClip() ||
        compound_controller.compoundNavigationDepth() != 2 ||
        compound_controller.compoundBreadcrumb() !=
            QStringLiteral("Interview Selects / Nested Selects") ||
        compound_controller.clipCount() != 2 || compound_controller.canUndo()) {
      return fail("compound timelines should support isolated nested navigation and undo history");
    }
    compound_controller.selectClip(0);
    compound_controller.setSelectedClipOpacity(0.25);
    compound_controller.seekSequenceSeconds(compound_controller.totalDurationSecondsValue());
    compound_controller.addTitleClip();
    if (compound_controller.clipCount() != 3 || !compound_controller.canUndo() ||
        !compound_controller.closeCompoundClip() ||
        compound_controller.compoundNavigationDepth() != 1 ||
        compound_controller.clipCount() != 1) {
      return fail("returning from a nested compound should save its edits and restore parent history");
    }
    compound_controller.seekSequenceSeconds(compound_controller.totalDurationSecondsValue());
    compound_controller.addTitleClip();
    if (compound_controller.clipCount() != 2 || !compound_controller.closeCompoundClip() ||
        compound_controller.insideCompound() || compound_controller.clipCount() != 2 ||
        !compound_controller.currentClip().value(QStringLiteral("isCompound")).toBool() ||
        !wait_for_compound(&compound_controller)) {
      return fail("nested compound updates should queue in dependency order and refresh the parent preview");
    }
    const auto updated_parent_compound = compound_controller.currentClip();
    const auto parent_tracks_after_return = compound_controller.sequenceTracks();
    const auto parent_video_clips_after_return =
        parent_tracks_after_return.at(0).toMap().value(QStringLiteral("clips")).toList();
    if (updated_parent_compound.value(QStringLiteral("durationSeconds")).toDouble() <=
            original_compound_duration + 0.5 ||
        !nearly_equal(
            updated_parent_compound.value(QStringLiteral("trimmedDurationSeconds")).toDouble(),
            original_compound_duration,
            0.02) ||
        parent_video_clips_after_return.size() != 2 ||
        !nearly_equal(
            parent_video_clips_after_return.at(1).toMap()
                .value(QStringLiteral("startSeconds")).toDouble(),
            downstream_start,
            0.02)) {
      return fail("child duration changes should update source duration without moving the parent edit or downstream clips");
    }
    const auto nested_parent_render_path =
        workspace.filePath(QStringLiteral("nested-compound-parent.mp4"));
    if (!compound_controller.renderCurrentTimeline(nested_parent_render_path)) {
      return fail("the parent sequence should export after dependency-ordered nested regeneration");
    }
    const auto nested_parent_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.40"),
            QStringLiteral("-i"), nested_parent_render_path,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (nested_parent_sample.size() < 3 ||
        static_cast<unsigned char>(nested_parent_sample.at(0)) > 30 ||
        static_cast<unsigned char>(nested_parent_sample.at(1)) > 30 ||
        static_cast<unsigned char>(nested_parent_sample.at(2)) < 30 ||
        static_cast<unsigned char>(nested_parent_sample.at(2)) > 120) {
      return fail("final parent export should contain the visible nested child opacity edit rather than a stale preview");
    }
    if (!compound_controller.openSelectedCompoundClip() || compound_controller.clipCount() != 2 ||
        !compound_controller.openSelectedCompoundClip() || compound_controller.clipCount() != 3 ||
        !compound_controller.closeCompoundClip() || !compound_controller.closeCompoundClip() ||
        !wait_for_compound(&compound_controller)) {
      return fail("nested child edits should survive repeated open and return navigation");
    }
    if (!compound_controller.saveTimeline()) {
      return fail("a parent sequence containing a compound clip should save");
    }
    TimelineDocumentController reopened_compound_controller;
    if (!reopened_compound_controller.loadTimeline(compound_controller.savedDocumentPath()) ||
        !reopened_compound_controller.currentClip().value(QStringLiteral("isCompound")).toBool() ||
        !reopened_compound_controller.openSelectedCompoundClip() ||
        reopened_compound_controller.clipCount() != 2 ||
        !reopened_compound_controller.openSelectedCompoundClip() ||
        reopened_compound_controller.clipCount() != 3 ||
        !reopened_compound_controller.closeCompoundClip() ||
        !reopened_compound_controller.closeCompoundClip()) {
      return fail("nested compound metadata and editable child documents should survive parent save and reopen");
    }

    TimelineDocumentController source_patch_controller;
    source_patch_controller.createTimeline(QStringLiteral("Source Patch Timeline"));
    source_patch_controller.addVideoTrack();
    source_patch_controller.loadSourceClipFromGallery(
        QStringLiteral("video:source-patch"),
        render_video_overlay,
        QStringLiteral("Patched Source"),
        0.0,
        QStringLiteral("poster://source-patch"));
    if (!wait_for_media_analysis(&source_patch_controller)) {
      return fail("source patch analysis should finish in the background");
    }
    if (!source_patch_controller.sourcePatchReady() ||
        source_patch_controller.sourcePatchSummary() != QStringLiteral("V1 · A1")) {
      return fail("a loaded A/V source should expose the default independent V1 and A1 patches");
    }

    source_patch_controller.toggleTrackTargeted(1);
    source_patch_controller.toggleTrackTargeted(3);
    auto source_patch_tracks = source_patch_controller.sequenceTracks();
    if (source_patch_controller.sourcePatchSummary() != QStringLiteral("V2 · A2") ||
        source_patch_tracks.at(0).toMap().value(QStringLiteral("targeted")).toBool() ||
        !source_patch_tracks.at(1).toMap().value(QStringLiteral("targeted")).toBool() ||
        source_patch_tracks.at(2).toMap().value(QStringLiteral("targeted")).toBool() ||
        !source_patch_tracks.at(3).toMap().value(QStringLiteral("targeted")).toBool()) {
      return fail("video and audio patch controls should select one destination independently per stream");
    }
    source_patch_controller.undo();
    if (source_patch_controller.sourcePatchSummary() != QStringLiteral("V2 · A1")) {
      return fail("undo should restore only the most recent source-patch destination change");
    }
    source_patch_controller.redo();
    if (source_patch_controller.sourcePatchSummary() != QStringLiteral("V2 · A2")) {
      return fail("redo should restore an independent audio patch destination");
    }

    source_patch_controller.insertSourceClipAtPlayhead();
    source_patch_tracks = source_patch_controller.sequenceTracks();
    const auto patched_video_clips =
        source_patch_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    const auto patched_audio_clips =
        source_patch_tracks.at(3).toMap().value(QStringLiteral("clips")).toList();
    if (source_patch_tracks.at(0).toMap().value(QStringLiteral("clipCount")).toInt() != 0 ||
        patched_video_clips.size() != 1 ||
        source_patch_tracks.at(2).toMap().value(QStringLiteral("clipCount")).toInt() != 0 ||
        patched_audio_clips.size() != 1 ||
        source_patch_controller.currentClip().value(QStringLiteral("trackLabel")).toString() !=
            QStringLiteral("V2") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioTrackLabel")).toString() !=
            QStringLiteral("A2") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioLaneIndex")).toInt() != 1 ||
        !source_patch_controller.currentClip().value(QStringLiteral("audioLinked")).toBool() ||
        patched_video_clips.first().toMap().value(QStringLiteral("linkedGroupId")) !=
            patched_audio_clips.first().toMap().value(QStringLiteral("linkedGroupId"))) {
      return fail("source insert should route linked picture to V2 and sound to A2 without hidden fallback");
    }

    if (!source_patch_controller.moveTrackRow(1, -1) ||
        source_patch_controller.currentClip().value(QStringLiteral("trackLabel")).toString() !=
            QStringLiteral("V1") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioTrackLabel")).toString() !=
            QStringLiteral("A1") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioLaneIndex")).toInt() != 0) {
      return fail("paired track reorder should remap an explicit linked audio destination with its track identity");
    }
    source_patch_controller.undo();
    if (source_patch_controller.currentClip().value(QStringLiteral("trackLabel")).toString() !=
            QStringLiteral("V2") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioTrackLabel")).toString() !=
            QStringLiteral("A2") ||
        source_patch_controller.currentClip().value(QStringLiteral("audioLaneIndex")).toInt() != 1) {
      return fail("undo should restore paired picture and explicit audio routing after track reorder");
    }

    if (!source_patch_controller.saveTimeline()) {
      return fail("cross-lane source patch state should save with the timeline document");
    }
    TimelineDocumentController reopened_source_patch_controller;
    if (!reopened_source_patch_controller.loadTimeline(source_patch_controller.savedDocumentPath()) ||
        reopened_source_patch_controller.sourcePatchSummary() != QStringLiteral("V2 · A2") ||
        reopened_source_patch_controller.currentClip().value(QStringLiteral("audioTrackLabel")).toString() !=
            QStringLiteral("A2") ||
        reopened_source_patch_controller.currentClip().value(QStringLiteral("audioLaneIndex")).toInt() != 1) {
      return fail("source patch destinations and cross-lane linked audio should survive save and reopen");
    }

    TimelineDocumentController linked_clipboard_controller;
    linked_clipboard_controller.createTimeline(QStringLiteral("Linked Clipboard"));
    linked_clipboard_controller.addVideoTrack();
    linked_clipboard_controller.loadSourceClipFromGallery(
        QStringLiteral("video:linked-clipboard"),
        render_video_overlay,
        QStringLiteral("Linked Clipboard Source"),
        0.0,
        QStringLiteral("poster://linked-clipboard"));
    if (!wait_for_media_analysis(&linked_clipboard_controller)) {
      return fail("linked clipboard source analysis should finish in the background");
    }
    linked_clipboard_controller.insertSourceClipAtPlayhead();
    if (linked_clipboard_controller.copySelectedClips() != 1) {
      return fail("copy should capture one linked A/V edit through its video selection identity");
    }
    linked_clipboard_controller.toggleTrackTargeted(1);
    linked_clipboard_controller.toggleTrackTargeted(3);
    linked_clipboard_controller.seekSequenceSeconds(
        linked_clipboard_controller.totalDurationSecondsValue());
    if (linked_clipboard_controller.pasteClipsAtPlayhead(QStringLiteral("overwrite")) != 1) {
      return fail("linked clipboard paste should accept independently targeted V2 and A2 destinations");
    }
    const auto linked_clipboard_tracks = linked_clipboard_controller.sequenceTracks();
    const auto linked_clipboard_video =
        linked_clipboard_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    const auto linked_clipboard_audio =
        linked_clipboard_tracks.at(3).toMap().value(QStringLiteral("clips")).toList();
    if (linked_clipboard_video.size() != 1 || linked_clipboard_audio.size() != 1 ||
        linked_clipboard_video.first().toMap().value(QStringLiteral("clipId")) !=
            linked_clipboard_audio.first().toMap().value(QStringLiteral("clipId")) ||
        linked_clipboard_video.first().toMap().value(QStringLiteral("linkedGroupId")) !=
            linked_clipboard_audio.first().toMap().value(QStringLiteral("linkedGroupId")) ||
        linked_clipboard_video.first().toMap().value(QStringLiteral("audioLaneIndex")).toInt() != 1) {
      return fail("paste should preserve linked identity while remapping picture and sound to targeted lanes");
    }
    linked_clipboard_controller.undo();
    if (linked_clipboard_controller.clipCount() != 1 ||
        linked_clipboard_controller.sequenceTracks().at(3).toMap()
                .value(QStringLiteral("clipCount")).toInt() != 0) {
      return fail("linked A/V paste should undo picture and sound atomically");
    }

    source_patch_controller.toggleTrackTargeted(3);
    source_patch_controller.seekSequenceSeconds(1.20);
    source_patch_controller.insertSourceClipAtPlayhead();
    source_patch_tracks = source_patch_controller.sequenceTracks();
    if (source_patch_controller.sourcePatchSummary() != QStringLiteral("V2 · A off")) {
      return fail("disabling the audio patch should leave V2 as the only enabled source stream");
    }
    if (source_patch_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 2 ||
        source_patch_tracks.at(3).toMap().value(QStringLiteral("clipCount")).toInt() != 1) {
      return fail("a video-only source edit should add picture without adding a second sound component");
    }
    if (source_patch_controller.currentClip().value(QStringLiteral("hasAudio")).toBool()) {
      return fail("disabling the audio patch should mark the inserted picture as video-only");
    }
    source_patch_controller.undo();

    source_patch_controller.toggleTrackTargeted(3);
    source_patch_controller.toggleTrackTargeted(1);
    source_patch_controller.seekSequenceSeconds(1.20);
    source_patch_controller.insertSourceClipAtPlayhead();
    source_patch_tracks = source_patch_controller.sequenceTracks();
    const auto audio_only_clips =
        source_patch_tracks.at(3).toMap().value(QStringLiteral("clips")).toList();
    if (source_patch_controller.sourcePatchSummary() != QStringLiteral("V off · A2") ||
        !source_patch_controller.sourcePatchReady() ||
        source_patch_tracks.at(1).toMap().value(QStringLiteral("clipCount")).toInt() != 1 ||
        audio_only_clips.size() != 2 ||
        audio_only_clips.last().toMap().value(QStringLiteral("mediaType")).toString() !=
            QStringLiteral("audio") ||
        !audio_only_clips.last().toMap().value(QStringLiteral("displayLabel")).toString().endsWith(
            QStringLiteral(" Audio"))) {
      return fail("disabling the video patch should create a deterministic audio-only edit on A2");
    }

    TimelineDocumentController cross_lane_overwrite_controller;
    cross_lane_overwrite_controller.createTimeline(QStringLiteral("Cross-lane Overwrite Timeline"));
    cross_lane_overwrite_controller.addVideoTrack();
    cross_lane_overwrite_controller.toggleTrackTargeted(1);
    cross_lane_overwrite_controller.toggleTrackTargeted(3);
    cross_lane_overwrite_controller.loadSourceClipFromGallery(
        QStringLiteral("video:cross-lane-overwrite"),
        render_video_overlay,
        QStringLiteral("Cross-lane Source"),
        0.0,
        QStringLiteral("poster://cross-lane-overwrite"));
    if (!wait_for_media_analysis(&cross_lane_overwrite_controller)) {
      return fail("cross-lane source analysis should finish in the background");
    }
    cross_lane_overwrite_controller.insertSourceClipAtPlayhead();
    cross_lane_overwrite_controller.seekSequenceSeconds(0.40);
    cross_lane_overwrite_controller.setSourceInSeconds(0.10);
    cross_lane_overwrite_controller.setSourceOutSeconds(0.50);
    cross_lane_overwrite_controller.overwriteSourceClipAtPlayhead();
    const auto cross_lane_tracks = cross_lane_overwrite_controller.sequenceTracks();
    const auto cross_lane_video =
        cross_lane_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    const auto cross_lane_audio =
        cross_lane_tracks.at(3).toMap().value(QStringLiteral("clips")).toList();
    if (cross_lane_video.size() != cross_lane_audio.size() || cross_lane_video.size() != 3) {
      return fail("cross-lane overwrite should preserve aligned leading, replacement, and trailing A/V segments");
    }
    for (int index = 0; index < cross_lane_video.size(); ++index) {
      const auto video_clip = cross_lane_video.at(index).toMap();
      const auto audio_clip = cross_lane_audio.at(index).toMap();
      if (video_clip.value(QStringLiteral("clipId")) != audio_clip.value(QStringLiteral("clipId")) ||
          !nearly_equal(video_clip.value(QStringLiteral("startSeconds")).toDouble(),
                        audio_clip.value(QStringLiteral("startSeconds")).toDouble()) ||
          !nearly_equal(video_clip.value(QStringLiteral("endSeconds")).toDouble(),
                        audio_clip.value(QStringLiteral("endSeconds")).toDouble())) {
        return fail("cross-lane overwrite should keep picture and patched audio frame-aligned");
      }
    }

    TimelineDocumentController linked_edit_controller;
    linked_edit_controller.createTimeline(QStringLiteral("Linked A/V Timeline"));
    linked_edit_controller.addClipFromGallery(
        QStringLiteral("video:linked"),
        render_video_overlay,
        QStringLiteral("Linked Source"),
        0.0,
        QStringLiteral("poster://linked"));
    auto linked_tracks = linked_edit_controller.sequenceTracks();
    auto linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!linked_edit_controller.currentClip().value(QStringLiteral("audioLinked")).toBool() ||
        linked_audio_clips.size() != 1 ||
        !nearly_equal(linked_audio_clips.first().toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0)) {
      return fail("embedded picture and sound should enter the timeline as a visible linked pair");
    }

    linked_edit_controller.moveSelectedClipToTrackRow(0, 0.25);
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!nearly_equal(linked_edit_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.25) ||
        !nearly_equal(linked_audio_clips.first().toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.25)) {
      return fail("moving linked video should move its audio by the exact same timeline delta");
    }
    linked_edit_controller.undo();

    linked_edit_controller.trimSelectedClipEndToSequenceSeconds(0.90);
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!nearly_equal(linked_edit_controller.currentClip().value(QStringLiteral("endSeconds")).toDouble(), 0.90) ||
        !nearly_equal(linked_audio_clips.first().toMap().value(QStringLiteral("endSeconds")).toDouble(), 0.90)) {
      return fail("trimming linked video should preserve frame-accurate audio alignment");
    }
    linked_edit_controller.undo();

    if (!linked_edit_controller.splitSelectedClip(0.60)) {
      return fail("splitting a linked clip should succeed at a valid edit point");
    }
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (linked_edit_controller.clipCount() != 2 || linked_audio_clips.size() != 2 ||
        !nearly_equal(linked_audio_clips.at(0).toMap().value(QStringLiteral("endSeconds")).toDouble(), 0.60) ||
        !nearly_equal(linked_audio_clips.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.60)) {
      return fail("splitting linked media should create aligned picture and sound segments");
    }
    linked_edit_controller.undo();

    if (!linked_edit_controller.duplicateSelectedClip()) {
      return fail("duplicating a linked clip should succeed");
    }
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (linked_edit_controller.clipCount() != 2 || linked_audio_clips.size() != 2) {
      return fail("duplicating linked media should duplicate picture and sound together");
    }
    linked_edit_controller.removeSelectedClip();
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (linked_edit_controller.clipCount() != 1 || linked_audio_clips.size() != 1) {
      return fail("deleting linked media should remove picture and sound together");
    }

    linked_edit_controller.selectClip(0);
    if (!linked_edit_controller.unlinkSelectedClipAudio()) {
      return fail("an embedded audio component should support explicit unlinking");
    }
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!linked_edit_controller.currentClip().value(QStringLiteral("audioDetached")).toBool() ||
        linked_edit_controller.currentClip().value(QStringLiteral("audioLinked")).toBool() ||
        linked_audio_clips.size() != 1 ||
        linked_audio_clips.first().toMap().value(QStringLiteral("mediaType")).toString() != QStringLiteral("audio")) {
      return fail("unlinking should preserve sound as one independent audio clip without a silent duplicate");
    }
    linked_edit_controller.moveSelectedClipToTrackRow(0, 0.35);
    linked_tracks = linked_edit_controller.sequenceTracks();
    linked_audio_clips = linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!nearly_equal(linked_edit_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.35) ||
        !nearly_equal(linked_audio_clips.first().toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0)) {
      return fail("an unlinked video edit should leave its detached audio at the original time");
    }
    linked_edit_controller.undo();
    if (!linked_edit_controller.saveTimeline()) {
      return fail("detached media state should save to the timeline document");
    }
    TimelineDocumentController reopened_linked_edit_controller;
    if (!reopened_linked_edit_controller.loadTimeline(linked_edit_controller.savedDocumentPath())) {
      return fail("detached media state should reopen from the timeline document");
    }
    const auto reopened_linked_tracks = reopened_linked_edit_controller.sequenceTracks();
    const auto reopened_linked_audio = reopened_linked_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!reopened_linked_edit_controller.currentClip().value(QStringLiteral("audioDetached")).toBool() ||
        reopened_linked_audio.size() != 1 ||
        reopened_linked_audio.first().toMap().value(QStringLiteral("mediaType")).toString() != QStringLiteral("audio")) {
      return fail("save and reopen should preserve the independent audio component without duplication");
    }
    if (!reopened_linked_edit_controller.relinkSelectedClipAudio() ||
        !reopened_linked_edit_controller.currentClip().value(QStringLiteral("audioLinked")).toBool()) {
      return fail("detached audio should relink to its matching video clip after reopen");
    }

    TimelineDocumentController linked_override_controller;
    linked_override_controller.createTimeline(QStringLiteral("Linked Override Timeline"));
    linked_override_controller.addClipFromGallery(
        QStringLiteral("video:linked-override"),
        render_video_overlay,
        QStringLiteral("Override Source"),
        0.0,
        QStringLiteral("poster://linked-override"));
    if (!linked_override_controller.nudgeSelectedTrimEdge(
            QStringLiteral("end"), -12, true, false)) {
      return fail("Option keyboard trim should accept an asymmetric twelve-frame Out nudge");
    }
    const auto override_tracks = linked_override_controller.sequenceTracks();
    const auto override_audio_clips = override_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!linked_override_controller.currentClip().value(QStringLiteral("audioDetached")).toBool() ||
        !nearly_equal(linked_override_controller.currentClip().value(QStringLiteral("endSeconds")).toDouble(), 0.80) ||
        override_audio_clips.size() != 1 ||
        !nearly_equal(override_audio_clips.first().toMap().value(QStringLiteral("endSeconds")).toDouble(), 1.20)) {
      return fail("Option keyboard trim should adjust picture only while preserving the full audio component");
    }
    linked_override_controller.undo();
    if (!linked_override_controller.currentClip().value(QStringLiteral("audioLinked")).toBool()) {
      return fail("undo should atomically restore the linked pair after an independent override");
    }
    linked_override_controller.setLinkedSelectionEnabled(false);
    linked_override_controller.moveSelectedClipToTrackRow(0, 0.20);
    const auto unlinked_selection_tracks = linked_override_controller.sequenceTracks();
    const auto unlinked_selection_audio =
        unlinked_selection_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (!linked_override_controller.currentClip().value(QStringLiteral("audioDetached")).toBool() ||
        !nearly_equal(linked_override_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.20) ||
        !nearly_equal(unlinked_selection_audio.first().toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.0)) {
      return fail("disabling linked selection should edit picture independently without deleting sound");
    }

    TimelineDocumentController linked_overwrite_controller;
    linked_overwrite_controller.createTimeline(QStringLiteral("Linked Overwrite Timeline"));
    linked_overwrite_controller.addClipFromGallery(
        QStringLiteral("video:overwrite-base"),
        render_video_overlay,
        QStringLiteral("Overwrite Base"),
        0.0,
        QStringLiteral("poster://overwrite-base"));
    linked_overwrite_controller.seekSequenceSeconds(0.40);
    linked_overwrite_controller.overwriteClipFromGallery(
        QStringLiteral("video:overwrite-incoming"),
        render_video_overlay,
        QStringLiteral("Overwrite Incoming"),
        1.20,
        0.10,
        0.50,
        QStringLiteral("poster://overwrite-incoming"));
    const auto linked_overwrite_tracks = linked_overwrite_controller.sequenceTracks();
    const auto overwritten_video_clips =
        linked_overwrite_tracks.at(0).toMap().value(QStringLiteral("clips")).toList();
    const auto overwritten_audio_clips =
        linked_overwrite_tracks.at(1).toMap().value(QStringLiteral("clips")).toList();
    if (overwritten_video_clips.size() != overwritten_audio_clips.size()) {
      return fail("overwrite editing should preserve one aligned audio component for every linked video segment");
    }
    for (int index = 0; index < overwritten_video_clips.size(); ++index) {
      const auto video_clip = overwritten_video_clips.at(index).toMap();
      const auto audio_component = overwritten_audio_clips.at(index).toMap();
      if (video_clip.value(QStringLiteral("clipId")) != audio_component.value(QStringLiteral("clipId")) ||
          video_clip.value(QStringLiteral("linkedGroupId")) != audio_component.value(QStringLiteral("linkedGroupId")) ||
          !nearly_equal(video_clip.value(QStringLiteral("startSeconds")).toDouble(),
                        audio_component.value(QStringLiteral("startSeconds")).toDouble()) ||
          !nearly_equal(video_clip.value(QStringLiteral("endSeconds")).toDouble(),
                        audio_component.value(QStringLiteral("endSeconds")).toDouble())) {
        return fail("overwrite editing should keep linked group identity and exact picture/sound timing");
      }
    }

    TimelineDocumentController waveform_controller;
    waveform_controller.createTimeline(QStringLiteral("Waveform Timeline"));
    QElapsedTimer waveform_import_timer;
    waveform_import_timer.start();
    waveform_controller.addClipFromGallery(
        QStringLiteral("video:waveform"),
        render_video_one,
        QStringLiteral("Waveform Source"),
        0.0,
        QStringLiteral("poster://waveform"));
    const auto waveform_import_elapsed = waveform_import_timer.elapsed();
    if (!waveform_controller.currentClip().value(QStringLiteral("waveformSource")).toString().isEmpty()) {
      return fail("uncached waveform generation should not block the media import call");
    }
    if (waveform_import_elapsed > 1000 || !wait_for_waveform(&waveform_controller)) {
      return fail("waveform generation should complete asynchronously after a responsive import");
    }
    const auto generated_waveform =
        waveform_controller.currentClip().value(QStringLiteral("waveformSource")).toString();
    if (!QFileInfo::exists(QUrl(generated_waveform).toLocalFile())) {
      return fail("asynchronous waveform generation should publish a real cached image");
    }
    const auto generated_waveform_clip = waveform_controller.currentClip();
    const auto compact_waveform =
        generated_waveform_clip.value(QStringLiteral("waveformSourceCompact")).toString();
    const auto detail_waveform =
        generated_waveform_clip.value(QStringLiteral("waveformSourceDetail")).toString();
    const QImage compact_waveform_image(QUrl(compact_waveform).toLocalFile());
    const QImage standard_waveform_image(QUrl(generated_waveform).toLocalFile());
    const QImage detail_waveform_image(QUrl(detail_waveform).toLocalFile());
    if (generated_waveform_clip.value(QStringLiteral("waveformState")).toString() !=
            QStringLiteral("ready") ||
        compact_waveform_image.size() != QSize(600, 160) ||
        standard_waveform_image.size() != QSize(2400, 160) ||
        detail_waveform_image.size() != QSize(9600, 160)) {
      return fail("waveform generation should atomically publish compact, standard, and detail cache tiers");
    }
    if (qAlpha(detail_waveform_image.pixel(0, 0)) != 0) {
      return fail("cached waveform assets should keep their background transparent");
    }
    TimelineDocumentController waveform_cache_controller;
    waveform_cache_controller.createTimeline(QStringLiteral("Waveform Cache Timeline"));
    waveform_cache_controller.addClipFromGallery(
        QStringLiteral("video:waveform-cache"),
        render_video_one,
        QStringLiteral("Waveform Cache Source"),
        0.0,
        QStringLiteral("poster://waveform-cache"));
    const auto cached_waveform_clip = waveform_cache_controller.currentClip();
    if (cached_waveform_clip.value(QStringLiteral("waveformSource")).toString() != generated_waveform ||
        cached_waveform_clip.value(QStringLiteral("waveformSourceCompact")).toString() != compact_waveform ||
        cached_waveform_clip.value(QStringLiteral("waveformSourceDetail")).toString() != detail_waveform ||
        cached_waveform_clip.value(QStringLiteral("waveformState")).toString() != QStringLiteral("ready")) {
      return fail("all waveform detail tiers should be reused immediately on the next import");
    }

    const auto revision_video = workspace.filePath(QStringLiteral("waveform-revision.mp4"));
    if (!QFile::copy(render_video_one, revision_video)) {
      return fail("waveform revision fixture should copy its source media");
    }
    TimelineDocumentController waveform_revision_one;
    waveform_revision_one.createTimeline(QStringLiteral("Waveform Revision One"));
    waveform_revision_one.addClipFromGallery(
        QStringLiteral("video:waveform-revision-one"),
        revision_video,
        QStringLiteral("Waveform Revision"),
        0.0,
        QStringLiteral("poster://waveform-revision"));
    if (!wait_for_waveform(&waveform_revision_one, 30000)) {
      return fail("the first media revision should generate a complete waveform cache");
    }
    const auto first_revision_waveform =
        waveform_revision_one.currentClip().value(QStringLiteral("waveformSource")).toString();
    QFile revised_media(revision_video);
    if (!revised_media.open(QIODevice::Append) || revised_media.write("\0", 1) != 1) {
      return fail("waveform revision fixture should update the media fingerprint");
    }
    revised_media.close();
    TimelineDocumentController waveform_revision_two;
    waveform_revision_two.createTimeline(QStringLiteral("Waveform Revision Two"));
    waveform_revision_two.addClipFromGallery(
        QStringLiteral("video:waveform-revision-two"),
        revision_video,
        QStringLiteral("Waveform Revision"),
        0.0,
        QStringLiteral("poster://waveform-revision"));
    if (!waveform_revision_two.currentClip().value(QStringLiteral("waveformSource")).toString().isEmpty() ||
        !wait_for_waveform(&waveform_revision_two, 30000)) {
      return fail("a changed media revision should avoid stale waveform reuse and regenerate asynchronously");
    }
    const auto second_revision_waveform =
        waveform_revision_two.currentClip().value(QStringLiteral("waveformSource")).toString();
    if (second_revision_waveform == first_revision_waveform ||
        !QFileInfo::exists(QUrl(second_revision_waveform).toLocalFile())) {
      return fail("media revisions should produce distinct durable waveform cache identities");
    }

    TimelineDocumentController speed_render_controller;
    speed_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Speed Render Truth")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    speed_render_controller.addClipFromGallery(
        QStringLiteral("video:speed-render"), render_video_one,
        QStringLiteral("Speed Source"), 0.0, QString{});
    speed_render_controller.setSelectedClipPlaybackRate(2.0);
    const auto speed_render_path =
        workspace.filePath(QStringLiteral("speed-render-truth.mp4"));
    if (!nearly_equal(speed_render_controller.totalDurationSecondsValue(), 0.6, 0.02) ||
        !speed_render_controller.renderCurrentTimeline(speed_render_path)) {
      return fail("a retimed sequence should render at its source-duration divided by rate");
    }
    const auto speed_duration_text = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
            QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
            speed_render_path,
        });
    bool speed_duration_ok = false;
    const auto speed_duration = speed_duration_text.toDouble(&speed_duration_ok);
    if (!speed_duration_ok || !nearly_equal(speed_duration, 0.6, 0.10) ||
        detected_mean_volume_db(ffmpeg, speed_render_path, 660) < -45.0) {
      return fail("retimed export should match sequence duration and preserve audio pitch");
    }

    if (!speed_render_controller.setSelectedClipRetimeOptions(
            QStringLiteral("follow-speed"), QStringLiteral("frame-sampling"))) {
      return fail("the encoded speed fixture should accept follow-speed audio");
    }
    const auto follow_speed_render_path =
        workspace.filePath(QStringLiteral("follow-speed-render-truth.mp4"));
    if (!speed_render_controller.renderCurrentTimeline(follow_speed_render_path) ||
        detected_mean_volume_db(ffmpeg, follow_speed_render_path, 1320) < -45.0 ||
        detected_mean_volume_db(ffmpeg, follow_speed_render_path, 660) > -45.0) {
      return fail("follow-speed export should shift pitch with playback rate instead of preserving it");
    }

    if (!speed_render_controller.applySelectedClipRetimePreset(QStringLiteral("reverse"))) {
      return fail("the render fixture should accept reverse retiming");
    }
    const auto reverse_render_path =
        workspace.filePath(QStringLiteral("reverse-render-truth.mp4"));
    if (!speed_render_controller.renderCurrentTimeline(reverse_render_path)) {
      return fail("a reverse-retimed sequence should render through the shared export path");
    }
    const auto reverse_duration_text = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
            QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
            reverse_render_path,
        });
    bool reverse_duration_ok = false;
    const auto reverse_duration = reverse_duration_text.toDouble(&reverse_duration_ok);
    const auto reverse_mean_volume =
        detected_mean_volume_db(ffmpeg, reverse_render_path, 660);
    if (!reverse_duration_ok || !nearly_equal(reverse_duration, 0.6, 0.10) ||
        reverse_mean_volume > -90.0) {
      std::cerr << "reverse duration=" << reverse_duration
                << " mean_volume=" << reverse_mean_volume << '\n';
      return fail("reverse export should preserve clip duration and match the intentionally silent preview policy");
    }

    speed_render_controller.seekSequenceSeconds(0.3);
    if (!speed_render_controller.applySelectedClipRetimePreset(QStringLiteral("freeze-frame"))) {
      return fail("the render fixture should accept a freeze at the playhead");
    }
    const auto freeze_render_path =
        workspace.filePath(QStringLiteral("freeze-render-truth.mp4"));
    if (!speed_render_controller.renderCurrentTimeline(freeze_render_path)) {
      return fail("a held source frame should render through the shared export path");
    }
    const auto freeze_duration_text = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
            QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
            freeze_render_path,
        });
    bool freeze_duration_ok = false;
    const auto freeze_duration = freeze_duration_text.toDouble(&freeze_duration_ok);
    if (!freeze_duration_ok || !nearly_equal(freeze_duration, 1.6, 0.12)) {
      return fail("freeze export should add the exact held-frame duration to the sequence");
    }

    TimelineDocumentController professional_ramp_render_controller;
    professional_ramp_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Professional Ramp Render")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    professional_ramp_render_controller.addClipFromGallery(
        QStringLiteral("video:professional-ramp-render"), render_video_one,
        QStringLiteral("Professional Ramp Source"), 0.0, QString{});
    if (!professional_ramp_render_controller.applySelectedClipRetimePreset(
            QStringLiteral("ramp-up")) ||
        !professional_ramp_render_controller.setSelectedClipKeyframeBezierHandle(
            QStringLiteral("sourceTime"), 0.0, QStringLiteral("out"), 0.12, 0.22) ||
        !professional_ramp_render_controller.setSelectedClipRetimeOptions(
            QStringLiteral("preserve"), QStringLiteral("frame-blend"))) {
      return fail("the encoded ramp fixture should accept a Bezier curve and frame blending");
    }
    const auto professional_ramp_render_path =
        workspace.filePath(QStringLiteral("professional-ramp-render-truth.mp4"));
    if (!professional_ramp_render_controller.renderCurrentTimeline(
            professional_ramp_render_path)) {
      return fail("sampled Bezier speed ramps with frame blending should export through the shared renderer");
    }
    const auto professional_ramp_duration_text = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
            QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
            professional_ramp_render_path,
        });
    bool professional_ramp_duration_ok = false;
    const auto professional_ramp_duration =
        professional_ramp_duration_text.toDouble(&professional_ramp_duration_ok);
    const auto professional_ramp_cache_started =
        professional_ramp_render_controller.generateRenderCache();
    const auto professional_ramp_cache_ready =
        professional_ramp_cache_started &&
        wait_for_render_cache(&professional_ramp_render_controller);
    const auto professional_ramp_cache_path =
        QUrl(professional_ramp_render_controller.renderCachePlaybackSourceUrl()).toLocalFile();
    if (!professional_ramp_duration_ok ||
        !nearly_equal(professional_ramp_duration, 1.2, 0.12) ||
        !professional_ramp_cache_ready ||
        !QFileInfo::exists(professional_ramp_cache_path)) {
      std::cerr << "professional ramp duration=" << professional_ramp_duration
                << " duration_ok=" << professional_ramp_duration_ok
                << " cache_started=" << professional_ramp_cache_started
                << " cache_ready=" << professional_ramp_cache_ready
                << " cache_status="
                << professional_ramp_render_controller.renderCacheStatus().toStdString()
                << " cache_path=" << professional_ramp_cache_path.toStdString() << '\n';
      return fail("Bezier ramp export and the existing final-preview cache should share the authored duration");
    }

    TimelineDocumentController gain_fade_controller;
    gain_fade_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Gain Fade Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    gain_fade_controller.addClipFromGallery(
        QStringLiteral("video:gain-fade"), render_video_one,
        QStringLiteral("Gain Fade Clip"), 0.0, QString{});
    const auto baseline_gain_path =
        workspace.filePath(QStringLiteral("gain-fade-baseline.mp4"));
    if (!gain_fade_controller.renderCurrentTimeline(baseline_gain_path)) {
      return fail("baseline gain and fade fixture should render successfully");
    }
    const auto baseline_middle_db = detected_channel_mean_volume_db(
        ffmpeg, baseline_gain_path, 0, 0.48, 0.18);
    gain_fade_controller.setSelectedClipAudioGain(-6.0);
    gain_fade_controller.setSelectedClipFadeInSeconds(0.30);
    gain_fade_controller.setSelectedClipFadeOutSeconds(0.30);
    if (!gain_fade_controller.setTrackAutomationValue(
            1, QStringLiteral("audioGainDb"), -6.0)) {
      return fail("track gain should be editable for the gain and fade parity fixture");
    }
    const auto gain_fade_clip = gain_fade_controller.currentClip();
    if (!nearly_equal(gain_fade_clip.value(QStringLiteral("audioGainDb")).toDouble(), -6.0) ||
        !nearly_equal(gain_fade_clip.value(QStringLiteral("trackAudioGainDb")).toDouble(), -6.0) ||
        !nearly_equal(gain_fade_clip.value(QStringLiteral("fadeInSeconds")).toDouble(), 0.30) ||
        !nearly_equal(gain_fade_clip.value(QStringLiteral("fadeOutSeconds")).toDouble(), 0.30)) {
      return fail("Program state should expose the same clip gain, track gain, and fades used by export");
    }
    const auto gain_fade_path =
        workspace.filePath(QStringLiteral("gain-fade-output.mp4"));
    if (!gain_fade_controller.renderCurrentTimeline(gain_fade_path)) {
      return fail("attenuated and faded audio fixture should render successfully");
    }
    const auto faded_start_db = detected_channel_mean_volume_db(
        ffmpeg, gain_fade_path, 0, 0.02, 0.08);
    const auto attenuated_middle_db = detected_channel_mean_volume_db(
        ffmpeg, gain_fade_path, 0, 0.48, 0.18);
    const auto faded_end_db = detected_channel_mean_volume_db(
        ffmpeg, gain_fade_path, 0, 1.10, 0.08);
    if (attenuated_middle_db > baseline_middle_db - 9.0 ||
        attenuated_middle_db < baseline_middle_db - 15.0 ||
        faded_start_db > attenuated_middle_db - 8.0 ||
        faded_end_db > attenuated_middle_db - 5.0) {
      std::cerr << "gain/fade dB baseline=" << baseline_middle_db
                << " start=" << faded_start_db
                << " middle=" << attenuated_middle_db
                << " end=" << faded_end_db << '\n';
      return fail("exported clip and track gain plus head and tail fades should match Program state");
    }

    TimelineDocumentController pan_controller;
    pan_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Pan Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    pan_controller.addClipFromGallery(
        QStringLiteral("video:pan"), render_video_one, QStringLiteral("Panned Clip"), 0.0, QString{});
    if (!pan_controller.setTrackAutomationValue(1, QStringLiteral("audioPan"), 1.0) ||
        !nearly_equal(
            pan_controller.currentClip().value(QStringLiteral("trackAudioPan")).toDouble(), 1.0) ||
        pan_controller.currentClip().value(QStringLiteral("audioTrackRowIndex")).toInt() != 1) {
      return fail("audio tracks should expose their pan state and editable row to the active Program clip");
    }
    pan_controller.undo();
    if (!nearly_equal(
            pan_controller.currentClip().value(QStringLiteral("trackAudioPan")).toDouble(), 0.0)) {
      return fail("track pan should undo atomically");
    }
    pan_controller.redo();
    if (!pan_controller.saveTimeline()) {
      return fail("track pan fixture should save");
    }
    TimelineDocumentController reopened_pan_controller;
    if (!reopened_pan_controller.loadTimeline(pan_controller.savedDocumentPath()) ||
        !nearly_equal(
            reopened_pan_controller.currentClip()
                .value(QStringLiteral("trackAudioPan")).toDouble(),
            1.0)) {
      return fail("track pan should survive save and reopen");
    }
    const auto pan_render_path = workspace.filePath(QStringLiteral("pan-right-output.mp4"));
    if (!reopened_pan_controller.renderCurrentTimeline(pan_render_path)) {
      return fail("panned audio fixture should render successfully");
    }
    const auto left_channel_db = detected_channel_mean_volume_db(ffmpeg, pan_render_path, 0);
    const auto right_channel_db = detected_channel_mean_volume_db(ffmpeg, pan_render_path, 1);
    if (left_channel_db > -70.0 || right_channel_db < -35.0 ||
        right_channel_db < left_channel_db + 35.0) {
      return fail("full-right track pan should silence left export audio and preserve right audio");
    }

    reopened_pan_controller.seekSequenceSeconds(0.0);
    if (!reopened_pan_controller.toggleTrackAutomationKeyframe(
            1, QStringLiteral("audioPan"))) {
      return fail("track pan should support a keyframe at the playhead");
    }
    reopened_pan_controller.seekSequenceSeconds(0.60);
    if (!reopened_pan_controller.setTrackAutomationValue(
            1, QStringLiteral("audioPan"), -1.0)) {
      return fail("track pan automation should add a second evaluated point");
    }
    reopened_pan_controller.seekSequenceSeconds(0.30);
    const auto automated_audio_track =
        reopened_pan_controller.sequenceTracks().at(1).toMap();
    if (!nearly_equal(
            automated_audio_track.value(QStringLiteral("audioPan")).toDouble(), 0.0, 0.02) ||
        automated_audio_track.value(QStringLiteral("automationKeyframes")).toList().size() != 2) {
      return fail("track pan automation should interpolate continuously through center");
    }
    const auto automated_pan_path =
        workspace.filePath(QStringLiteral("pan-automation-output.mp4"));
    if (!reopened_pan_controller.renderCurrentTimeline(automated_pan_path)) {
      return fail("automated pan fixture should render successfully");
    }
    const auto early_left = detected_channel_mean_volume_db(
        ffmpeg, automated_pan_path, 0, 0.02, 0.12);
    const auto early_right = detected_channel_mean_volume_db(
        ffmpeg, automated_pan_path, 1, 0.02, 0.12);
    const auto late_left = detected_channel_mean_volume_db(
        ffmpeg, automated_pan_path, 0, 0.82, 0.18);
    const auto late_right = detected_channel_mean_volume_db(
        ffmpeg, automated_pan_path, 1, 0.82, 0.18);
    if (early_right < early_left + 8.0 || late_left < late_right + 25.0) {
      return fail("pan automation should move exported audio from right to left over sequence time");
    }

    TimelineDocumentController mixer_controller;
    mixer_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Bus Mixer Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    mixer_controller.addVideoTrack();
    mixer_controller.addClipFromGallery(
        QStringLiteral("video:dialogue-bus"), render_video_one,
        QStringLiteral("Dialogue"), 0.0, QString{});
    mixer_controller.addClipFromGallery(
        QStringLiteral("video:music-bus"), render_video_overlay,
        QStringLiteral("Music"), 0.0, QString{});
    mixer_controller.moveSelectedClipToTrackRow(1, 0.0);
    if (mixer_controller.sequenceTracks().size() != 4 ||
        !mixer_controller.setTrackAudioSetting(2, QStringLiteral("audioRole"), QStringLiteral("voiceover")) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioRole"), QStringLiteral("music")) ||
        !mixer_controller.setTrackAutomationValue(2, QStringLiteral("audioPan"), -1.0) ||
        !mixer_controller.setTrackAutomationValue(3, QStringLiteral("audioPan"), 1.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioEqMidDb"), 6.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioCompressorEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(
            3, QStringLiteral("audioCompressorThresholdDb"), -12.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioCompressorRatio"), 3.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioLimiterEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioLimiterCeilingDb"), -2.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioGateEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioGateThresholdDb"), -55.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioDeEsserEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioDeEsserAmount"), 0.25) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioHighPassEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioHighPassHz"), 120.0) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioLowPassEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(3, QStringLiteral("audioLowPassHz"), 14000.0) ||
        !mixer_controller.setTrackAudioSetting(
            3, QStringLiteral("audioProcessingBypassed"), true) ||
        !mixer_controller.setTrackAudioSetting(
            3, QStringLiteral("audioProcessingBypassed"), false)) {
      return fail("audio tracks should expose the complete core processor stack and matched bypass");
    }
    mixer_controller.seekSequenceSeconds(0.0);
    if (!mixer_controller.toggleTrackAutomationKeyframe(
            3, QStringLiteral("audioLimiterCeilingDb")) ||
        !mixer_controller.setTrackAutomationValue(
            3, QStringLiteral("audioLimiterCeilingDb"), -3.0)) {
      return fail("core audio processor values should author track automation");
    }
    mixer_controller.seekSequenceSeconds(0.5);
    if (!mixer_controller.setTrackAutomationValue(
            3, QStringLiteral("audioLimiterCeilingDb"), -1.0)) {
      return fail("audio processor automation should add an interpolated point at the playhead");
    }
    const auto configured_music_track = mixer_controller.sequenceTracks().at(3).toMap();
    if (configured_music_track.value(QStringLiteral("audioRole")).toString() != QStringLiteral("music") ||
        configured_music_track.value(QStringLiteral("audioBus")).toString() != QStringLiteral("music") ||
        !nearly_equal(configured_music_track.value(QStringLiteral("audioEqMidDb")).toDouble(), 6.0) ||
        !configured_music_track.value(QStringLiteral("audioCompressorEnabled")).toBool() ||
        !nearly_equal(
            configured_music_track.value(QStringLiteral("audioCompressorThresholdDb")).toDouble(),
            -12.0) ||
        !nearly_equal(
            configured_music_track.value(QStringLiteral("audioCompressorRatio")).toDouble(),
            3.0) ||
        !configured_music_track.value(QStringLiteral("audioLimiterEnabled")).toBool() ||
        !configured_music_track.value(QStringLiteral("audioGateEnabled")).toBool() ||
        !configured_music_track.value(QStringLiteral("audioDeEsserEnabled")).toBool() ||
        !configured_music_track.value(QStringLiteral("audioHighPassEnabled")).toBool() ||
        !configured_music_track.value(QStringLiteral("audioLowPassEnabled")).toBool() ||
        configured_music_track.value(QStringLiteral("audioProcessingBypassed")).toBool() ||
        !configured_music_track.value(QStringLiteral("audioLoudnessMatchedBypass")).toBool() ||
        configured_music_track.value(QStringLiteral("automationKeyframes")).toList().size() != 2 ||
        !nearly_equal(
            configured_music_track.value(QStringLiteral("audioLimiterCeilingDb")).toDouble(),
            -1.0)) {
      return fail("sequence state should publish the complete configured track processing graph");
    }

    const QVariantMap dialogue_cleanup_settings{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("voiceIsolationAmount"), 0.78},
        {QStringLiteral("noiseReductionAmount"), 0.42},
        {QStringLiteral("dereverbAmount"), 0.31},
        {QStringLiteral("humRemovalAmount"), 0.66},
        {QStringLiteral("humFrequencyHz"), 60},
        {QStringLiteral("artifactSmoothing"), 0.82},
        {QStringLiteral("ambiencePreservation"), 0.24},
    };
    const auto document_hash_before_cleanup_preview =
        mixer_controller.deterministicDocumentHash();
    if (!mixer_controller.previewTrackDialogueCleanup(
            2, dialogue_cleanup_settings, false)) {
      return fail("dialogue cleanup should stage a non-destructive Cleaned preview");
    }
    const auto cleaned_preview_track = mixer_controller.sequenceTracks().at(2).toMap();
    if (!cleaned_preview_track.value(QStringLiteral("dialogueCleanupPreviewActive")).toBool() ||
        cleaned_preview_track.value(QStringLiteral("dialogueCleanupAuditionOriginal")).toBool() ||
        !cleaned_preview_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        !nearly_equal(
            cleaned_preview_track.value(QStringLiteral("dialogueVoiceIsolationAmount")).toDouble(),
            0.78) ||
        mixer_controller.deterministicDocumentHash() !=
            document_hash_before_cleanup_preview) {
      return fail("Cleaned preview should publish staged settings without mutating project state");
    }
    if (!mixer_controller.previewTrackDialogueCleanup(
            2, dialogue_cleanup_settings, true)) {
      return fail("dialogue cleanup should support an Original A/B audition");
    }
    const auto original_preview_track = mixer_controller.sequenceTracks().at(2).toMap();
    if (!original_preview_track.value(QStringLiteral("dialogueCleanupPreviewActive")).toBool() ||
        !original_preview_track.value(QStringLiteral("dialogueCleanupAuditionOriginal")).toBool() ||
        original_preview_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        mixer_controller.deterministicDocumentHash() !=
            document_hash_before_cleanup_preview) {
      return fail("Original A/B audition should bypass only the transient Program processing path");
    }
    if (!mixer_controller.applyTrackDialogueCleanup(2, dialogue_cleanup_settings)) {
      return fail("dialogue cleanup Apply should commit its staged controls atomically");
    }
    const auto applied_cleanup_track = mixer_controller.sequenceTracks().at(2).toMap();
    if (applied_cleanup_track.value(QStringLiteral("dialogueCleanupPreviewActive")).toBool() ||
        !applied_cleanup_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        !nearly_equal(
            applied_cleanup_track.value(QStringLiteral("dialogueNoiseReductionAmount")).toDouble(),
            0.42) ||
        !nearly_equal(
            applied_cleanup_track.value(QStringLiteral("dialogueDereverbAmount")).toDouble(),
            0.31) ||
        !nearly_equal(
            applied_cleanup_track.value(QStringLiteral("dialogueArtifactSmoothing")).toDouble(),
            0.82)) {
      return fail("applied dialogue cleanup should expose every cleanup and artifact control");
    }
    mixer_controller.undo();
    if (mixer_controller.sequenceTracks().at(2).toMap()
            .value(QStringLiteral("dialogueCleanupEnabled")).toBool()) {
      return fail("dialogue cleanup Apply should undo in one history step");
    }
    mixer_controller.redo();
    if (!mixer_controller.sequenceTracks().at(2).toMap()
             .value(QStringLiteral("dialogueCleanupEnabled")).toBool()) {
      return fail("dialogue cleanup Apply should redo in one history step");
    }
    mixer_controller.seekSequenceSeconds(0.25);
    if (!mixer_controller.toggleTrackAutomationKeyframe(
            3, QStringLiteral("audioGainDb")) ||
        !mixer_controller.setTrackAutomationValue(
            3, QStringLiteral("audioGainDb"), -2.0)) {
      return fail("manual music gain automation should be authorable before auto ducking");
    }
    const auto first_ducking =
        mixer_controller.generateDialogueDuckingAutomation(-12.0, 0.12, 0.35);
    const auto regenerated_ducking =
        mixer_controller.generateDialogueDuckingAutomation(-18.0, 0.12, 0.35);
    const auto ducked_music_points = mixer_controller.sequenceTracks().at(3).toMap()
                                         .value(QStringLiteral("automationKeyframes")).toList();
    auto has_preserved_manual_point = false;
    auto generated_ducking_points = 0;
    for (const auto& point_value : ducked_music_points) {
      const auto point = point_value.toMap();
      if (point.value(QStringLiteral("parameter")).toString() != QStringLiteral("audioGainDb")) {
        continue;
      }
      if (point.value(QStringLiteral("source")).toString() ==
          QStringLiteral("dialogue-ducking:v1")) {
        ++generated_ducking_points;
      } else if (nearly_equal(point.value(QStringLiteral("seconds")).toDouble(), 0.25) &&
                 nearly_equal(point.value(QStringLiteral("value")).toDouble(), -2.0)) {
        has_preserved_manual_point = true;
      }
    }
    if (!first_ducking.value(QStringLiteral("applied")).toBool() ||
        !regenerated_ducking.value(QStringLiteral("applied")).toBool() ||
        regenerated_ducking.value(QStringLiteral("trackCount")).toInt() != 1 ||
        regenerated_ducking.value(QStringLiteral("preservedManualPointCount")).toInt() != 1 ||
        generated_ducking_points < 2 || !has_preserved_manual_point) {
      return fail("role-based ducking should regenerate its own envelope without destroying manual automation");
    }
    const auto voiceover_summary =
        mixer_controller.audioRoleSummary(QStringLiteral("voiceover"));
    if (voiceover_summary.value(QStringLiteral("trackCount")).toInt() != 1 ||
        voiceover_summary.value(QStringLiteral("defaultBus")).toString() !=
            QStringLiteral("dialogue") ||
        !mixer_controller.setAudioRoleSetting(
            QStringLiteral("voiceover"), QStringLiteral("muted"), true) ||
        !mixer_controller.sequenceTracks().at(2).toMap()
             .value(QStringLiteral("muted")).toBool()) {
      return fail("audio roles should publish routing summaries and apply one batch control to every matching track");
    }
    mixer_controller.undo();
    if (mixer_controller.sequenceTracks().at(2).toMap()
            .value(QStringLiteral("muted")).toBool()) {
      return fail("role-based batch controls should undo atomically");
    }
    const auto unducked_mix_path = workspace.filePath(QStringLiteral("bus-mix-unducked.mp4"));
    if (!mixer_controller.renderCurrentTimeline(unducked_mix_path)) {
      return fail("the bus-routed EQ and compressor fixture should render");
    }
    const auto unducked_left_db = detected_channel_mean_volume_db(ffmpeg, unducked_mix_path, 0);
    const auto unducked_right_db = detected_channel_mean_volume_db(ffmpeg, unducked_mix_path, 1);
    if (!mixer_controller.setTrackAudioSetting(
            3, QStringLiteral("audioDuckingEnabled"), true) ||
        !mixer_controller.setTrackAudioSetting(
            3, QStringLiteral("audioDuckingAmountDb"), -18.0)) {
      return fail("a non-dialogue bus should support automatic dialogue ducking");
    }
    mixer_controller.seekSequenceSeconds(0.4);
    if (!nearly_equal(
            mixer_controller.currentClip()
                .value(QStringLiteral("trackAudioDuckingGainDb"))
                .toDouble(),
            -18.0)) {
      return fail("Program preview state should lower a ducked bus while dialogue is active");
    }
    const auto ducked_mix_path = workspace.filePath(QStringLiteral("bus-mix-ducked.mp4"));
    if (!mixer_controller.renderCurrentTimeline(ducked_mix_path)) {
      return fail("the dialogue-sidechain bus fixture should render");
    }
    const auto ducked_left_db = detected_channel_mean_volume_db(ffmpeg, ducked_mix_path, 0);
    const auto ducked_right_db = detected_channel_mean_volume_db(ffmpeg, ducked_mix_path, 1);
    if (ducked_right_db > unducked_right_db - 5.0 ||
        qAbs(ducked_left_db - unducked_left_db) > 2.0) {
      std::cerr << "bus mixer dB left unducked=" << unducked_left_db
                << " ducked=" << ducked_left_db
                << " right unducked=" << unducked_right_db
                << " ducked=" << ducked_right_db << '\n';
      return fail("dialogue sidechain should reduce the music bus without lowering dialogue");
    }
    mixer_controller.setDeliverySetting(
        QStringLiteral("encoderMode"), QStringLiteral("software"));
    if (!mixer_controller.setDeliverySetting(QStringLiteral("twoPass"), true) ||
        !mixer_controller.setDeliverySetting(QStringLiteral("audioStems"), true) ||
        !mixer_controller.setDeliverySetting(QStringLiteral("imageSequence"), true)) {
      return fail("professional delivery settings should be independently configurable");
    }
    const auto configured_delivery = mixer_controller.deliverySettings();
    if (configured_delivery.value(QStringLiteral("resolvedEncoder")).toString() !=
            QStringLiteral("libx264") ||
        !configured_delivery.value(QStringLiteral("effectiveTwoPass")).toBool() ||
        configured_delivery.value(QStringLiteral("stemCount")).toInt() != 2 ||
        configured_delivery.value(QStringLiteral("packageArtifactCount")).toInt() != 4) {
      return fail("delivery state should resolve a two-pass software master plus isolated bus stems and frames");
    }
    if (!mixer_controller.saveTimeline()) {
      return fail("the configured bus mixer should save");
    }
    QFile saved_mixer_file(mixer_controller.savedDocumentPath());
    if (!saved_mixer_file.open(QIODevice::ReadOnly)) {
      return fail("saved mixer JSON should be readable");
    }
    const auto saved_mixer_tracks =
        QJsonDocument::fromJson(saved_mixer_file.readAll()).object()
            .value(QStringLiteral("tracks")).toArray();
    const auto saved_dialogue_track = saved_mixer_tracks.at(2).toObject();
    if (saved_mixer_tracks.at(3).toObject()
                .value(QStringLiteral("audioRole")).toString() !=
            QStringLiteral("music") ||
        !saved_dialogue_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        !nearly_equal(
            saved_dialogue_track.value(
                QStringLiteral("dialogueVoiceIsolationAmount")).toDouble(),
            0.78) ||
        saved_dialogue_track.value(QStringLiteral("dialogueHumFrequencyHz")).toInt() != 60 ||
        !nearly_equal(
            saved_dialogue_track.value(
                QStringLiteral("dialogueAmbiencePreservation")).toDouble(),
            0.24)) {
      return fail("saved mixer JSON should contain audio roles and the committed dialogue cleanup recipe");
    }
    const auto mixer_render_job_id = QStringLiteral("timeline-render-%1").arg(
        mixer_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(mixer_render_job_id, 120000)) {
      return fail("the professional delivery package should finish through the background timeline job");
    }
    const QFileInfo delivery_master_info(mixer_controller.savedRenderPath());
    const auto delivery_stem_root = QDir(delivery_master_info.absolutePath()).filePath(
        delivery_master_info.completeBaseName() + QStringLiteral("-stems"));
    const auto dialogue_stem = QDir(delivery_stem_root).filePath(QStringLiteral("dialogue.wav"));
    const auto music_stem = QDir(delivery_stem_root).filePath(QStringLiteral("music.wav"));
    const auto delivery_frame_root = QDir(delivery_master_info.absolutePath()).filePath(
        delivery_master_info.completeBaseName() + QStringLiteral("-frames"));
    const auto delivery_frames = QDir(delivery_frame_root).entryList(
        {QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name);
    if (!delivery_master_info.exists() || !QFileInfo::exists(dialogue_stem) ||
        !QFileInfo::exists(music_stem) || delivery_frames.size() < 25 ||
        QFileInfo::exists(mixer_controller.savedRenderPath() + QStringLiteral(".passlog-0.log"))) {
      return fail("two-pass delivery should leave one playable master, isolated WAV stems, numbered PNG frames, and no pass logs");
    }
    if (detected_channel_mean_volume_db(ffmpeg, dialogue_stem, 0) <
            detected_channel_mean_volume_db(ffmpeg, dialogue_stem, 1) + 12.0 ||
        detected_channel_mean_volume_db(ffmpeg, music_stem, 1) <
            detected_channel_mean_volume_db(ffmpeg, music_stem, 0) + 12.0) {
      return fail("exported dialogue and music stems should preserve their isolated bus routing");
    }
    TimelineDocumentController reopened_mixer_controller;
    if (!reopened_mixer_controller.loadTimeline(mixer_controller.savedDocumentPath())) {
      return fail("the configured bus mixer should reopen");
    }
    const auto reopened_music_track = reopened_mixer_controller.sequenceTracks().at(3).toMap();
    const auto reopened_dialogue_track =
        reopened_mixer_controller.sequenceTracks().at(2).toMap();
    if (reopened_music_track.value(QStringLiteral("audioRole")).toString() !=
            QStringLiteral("music")) {
      return fail("audio role should survive save and reopen");
    }
    if (!reopened_dialogue_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool() ||
        !nearly_equal(
            reopened_dialogue_track.value(
                QStringLiteral("dialogueVoiceIsolationAmount")).toDouble(),
            0.78) ||
        !nearly_equal(
            reopened_dialogue_track.value(
                QStringLiteral("dialogueNoiseReductionAmount")).toDouble(),
            0.42) ||
        !nearly_equal(
            reopened_dialogue_track.value(
                QStringLiteral("dialogueDereverbAmount")).toDouble(),
            0.31) ||
        !nearly_equal(
            reopened_dialogue_track.value(
                QStringLiteral("dialogueArtifactSmoothing")).toDouble(),
            0.82) ||
        reopened_music_track.value(QStringLiteral("audioBus")).toString() != QStringLiteral("music") ||
        !reopened_music_track.value(QStringLiteral("audioDuckingEnabled")).toBool() ||
        !nearly_equal(
            reopened_music_track.value(QStringLiteral("audioDuckingAmountDb")).toDouble(),
            -18.0) ||
        !reopened_music_track.value(QStringLiteral("audioCompressorEnabled")).toBool() ||
        !reopened_music_track.value(QStringLiteral("audioLimiterEnabled")).toBool() ||
        !reopened_music_track.value(QStringLiteral("audioGateEnabled")).toBool() ||
        !reopened_music_track.value(QStringLiteral("audioDeEsserEnabled")).toBool() ||
        !reopened_music_track.value(QStringLiteral("audioHighPassEnabled")).toBool() ||
        !reopened_music_track.value(QStringLiteral("audioLowPassEnabled")).toBool() ||
        reopened_music_track.value(QStringLiteral("automationKeyframes")).toList().size() < 5 ||
        reopened_mixer_controller.deliverySettings().value(QStringLiteral("encoderMode")).toString() !=
            QStringLiteral("software") ||
        !reopened_mixer_controller.deliverySettings().value(QStringLiteral("twoPass")).toBool() ||
        !reopened_mixer_controller.deliverySettings().value(QStringLiteral("audioStems")).toBool() ||
        !reopened_mixer_controller.deliverySettings().value(QStringLiteral("imageSequence")).toBool()) {
      std::cerr << "reopened dialogue cleanup enabled="
                << reopened_dialogue_track.value(QStringLiteral("dialogueCleanupEnabled")).toBool()
                << " voice="
                << reopened_dialogue_track.value(QStringLiteral("dialogueVoiceIsolationAmount")).toDouble()
                << " noise="
                << reopened_dialogue_track.value(QStringLiteral("dialogueNoiseReductionAmount")).toDouble()
                << " dereverb="
                << reopened_dialogue_track.value(QStringLiteral("dialogueDereverbAmount")).toDouble()
                << " smoothing="
                << reopened_dialogue_track.value(QStringLiteral("dialogueArtifactSmoothing")).toDouble()
                << " music automation="
                << reopened_music_track.value(QStringLiteral("automationKeyframes")).toList().size()
                << " music ducking="
                << reopened_music_track.value(QStringLiteral("audioDuckingEnabled")).toBool()
                << " delivery="
                << reopened_mixer_controller.deliverySettings().value(QStringLiteral("encoderMode")).toString().toStdString()
                << "/"
                << reopened_mixer_controller.deliverySettings().value(QStringLiteral("twoPass")).toBool()
                << "/"
                << reopened_mixer_controller.deliverySettings().value(QStringLiteral("audioStems")).toBool()
                << "/"
                << reopened_mixer_controller.deliverySettings().value(QStringLiteral("imageSequence")).toBool()
                << '\n';
      return fail("dialogue cleanup, bus routing, dynamics, and ducking should survive save and reopen");
    }

    if (!reopened_mixer_controller.setDeliverySetting(QStringLiteral("twoPass"), false) ||
        !reopened_mixer_controller.setDeliverySetting(QStringLiteral("audioStems"), false) ||
        !reopened_mixer_controller.setDeliverySetting(QStringLiteral("imageSequence"), false) ||
        !reopened_mixer_controller.setDeliverySetting(
            QStringLiteral("encoderMode"), QStringLiteral("hardware"))) {
      return fail("delivery should allow switching from a two-pass package to a hardware master");
    }
    const auto hardware_capabilities = reopened_mixer_controller.deliveryCapabilities();
    const auto expected_hardware_encoder =
        hardware_capabilities.value(QStringLiteral("hardwareH264Available")).toBool()
            ? QStringLiteral("h264_videotoolbox")
            : QStringLiteral("libx264");
    if (reopened_mixer_controller.deliverySettings()
            .value(QStringLiteral("resolvedEncoder")).toString() != expected_hardware_encoder) {
      return fail("hardware delivery should publish its capability-aware encoder resolution");
    }
    const auto hardware_master = workspace.filePath(QStringLiteral("hardware-delivery.mp4"));
    if (!reopened_mixer_controller.renderCurrentTimeline(hardware_master) ||
        process_stdout(
            ffprobe,
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-select_streams"), QStringLiteral("v:0"),
             QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name"),
             QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
             hardware_master}) != QStringLiteral("h264")) {
      return fail("hardware delivery should render H.264 and fall back to software when initialization fails");
    }
    TimelineDocumentController alpha_delivery_controller;
    alpha_delivery_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Alpha Delivery Truth")},
        {QStringLiteral("width"), 160},
        {QStringLiteral("height"), 120},
        {QStringLiteral("frameRate"), 30.0},
    });
    alpha_delivery_controller.addClipFromGallery(
        QStringLiteral("photo:alpha-delivery"), alpha_delivery_image,
        QStringLiteral("Transparent Graphic"), 1.0, alpha_delivery_image);
    if (!alpha_delivery_controller.setDeliverySetting(QStringLiteral("alpha"), true) ||
        alpha_delivery_controller.exportContainer() != QStringLiteral("mov") ||
        alpha_delivery_controller.exportCodec() != QStringLiteral("prores")) {
      return fail("alpha delivery should resolve to a ProRes MOV master");
    }
    const auto alpha_master = workspace.filePath(QStringLiteral("alpha-delivery.mov"));
    if (!alpha_delivery_controller.renderCurrentTimeline(alpha_master)) {
      return fail("a transparent timeline should render through the ProRes 4444 delivery path");
    }
    const auto alpha_pixel_format = process_stdout(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"), QStringLiteral("stream=pix_fmt"),
         QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"),
         alpha_master});
    const auto transparent_corner = process_bytes(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-ss"), QStringLiteral("0.1"),
         QStringLiteral("-i"), alpha_master,
         QStringLiteral("-frames:v"), QStringLiteral("1"),
         QStringLiteral("-vf"), QStringLiteral("crop=1:1:0:0,format=rgba"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    const auto opaque_center = process_bytes(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-ss"), QStringLiteral("0.1"),
         QStringLiteral("-i"), alpha_master,
         QStringLiteral("-frames:v"), QStringLiteral("1"),
         QStringLiteral("-vf"), QStringLiteral("crop=1:1:80:60,format=rgba"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    if (!alpha_pixel_format.startsWith(QStringLiteral("yuva444")) ||
        transparent_corner.size() < 4 || opaque_center.size() < 4 ||
        static_cast<unsigned char>(transparent_corner.at(3)) > 8 ||
        static_cast<unsigned char>(opaque_center.at(3)) < 245) {
      std::cerr << "alpha delivery pix_fmt=" << alpha_pixel_format.toStdString()
                << " corner="
                << (transparent_corner.size() >= 4
                        ? static_cast<int>(static_cast<unsigned char>(transparent_corner.at(3)))
                        : -1)
                << " center="
                << (opaque_center.size() >= 4
                        ? static_cast<int>(static_cast<unsigned char>(opaque_center.at(3)))
                        : -1)
                << '\n';
      return fail("ProRes 4444 delivery should preserve transparent and opaque timeline pixels");
    }

    TimelineDocumentController auto_reframe_controller;
    auto_reframe_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Editable Auto Reframe Truth")},
        {QStringLiteral("width"), 180},
        {QStringLiteral("height"), 320},
        {QStringLiteral("frameRate"), 30.0},
    });
    auto_reframe_controller.addClipFromGallery(
        QStringLiteral("video:moving-subject"), moving_subject_video,
        QStringLiteral("Moving Subject"), 2.0, QString{});
    if (!wait_for_media_analysis(&auto_reframe_controller)) {
      return fail("Auto Reframe fixture metadata should resolve before subject analysis");
    }
    auto_reframe_controller.setSelectedFrameMode(QStringLiteral("fill"));
    const auto before_reframe = auto_reframe_controller.currentClip();
    qputenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT", QByteArrayLiteral("1"));
    if (!auto_reframe_controller.analyzeSelectedClipForAutoReframe(QStringLiteral("headroom")) ||
        !auto_reframe_controller.autoReframeBusy() ||
        !auto_reframe_controller.currentClip().value(QStringLiteral("keyframes")).toList().isEmpty() ||
        auto_reframe_controller.currentClip().value(QStringLiteral("frameMode")).toString() !=
            QStringLiteral("fill") ||
        !wait_for_auto_reframe(&auto_reframe_controller)) {
      qunsetenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT");
      return fail("Auto Reframe analysis should run asynchronously without mutating clip framing");
    }
    qunsetenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT");
    const auto reframe_suggestions = auto_reframe_controller.autoReframeSuggestions();
    if (reframe_suggestions.size() != 5 ||
        reframe_suggestions.first().toMap().value(QStringLiteral("subjectCenterX")).toDouble() >=
            reframe_suggestions.last().toMap().value(QStringLiteral("subjectCenterX")).toDouble() ||
        reframe_suggestions.first().toMap().value(QStringLiteral("frameOffsetX")).toDouble() <= 1.0 ||
        reframe_suggestions.last().toMap().value(QStringLiteral("frameOffsetX")).toDouble() >= -1.0) {
      if (!reframe_suggestions.isEmpty()) {
        std::cerr << "Auto Reframe suggestions: first center="
                  << reframe_suggestions.first().toMap()
                         .value(QStringLiteral("subjectCenterX")).toDouble()
                  << " first offset="
                  << reframe_suggestions.first().toMap()
                         .value(QStringLiteral("frameOffsetX")).toDouble()
                  << " last center="
                  << reframe_suggestions.last().toMap()
                         .value(QStringLiteral("subjectCenterX")).toDouble()
                  << " last offset="
                  << reframe_suggestions.last().toMap()
                         .value(QStringLiteral("frameOffsetX")).toDouble()
                  << '\n';
      } else {
        std::cerr << "Auto Reframe status: "
                  << auto_reframe_controller.autoReframeStatus().toStdString() << '\n';
      }
      return fail("Auto Reframe should detect subject motion and propose an unclipped opposing pan");
    }
    if (!auto_reframe_controller.applyAutoReframeSuggestions()) {
      return fail("reviewed Auto Reframe suggestions should apply as one edit");
    }
    const auto applied_reframe = auto_reframe_controller.currentClip();
    const auto applied_keyframes = applied_reframe.value(QStringLiteral("keyframes")).toList();
    if (applied_reframe.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fit") ||
        applied_keyframes.size() != 15) {
      return fail("Auto Reframe should preserve the full source and create scale, X, and Y keyframes");
    }
    for (const auto& keyframe_value : applied_keyframes) {
      if (keyframe_value.toMap().value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("ease-in-out")) {
        return fail("Auto Reframe keyframes should remain ordinary editable eased keyframes");
      }
    }
    const auto middle_seconds =
        reframe_suggestions.at(2).toMap().value(QStringLiteral("localSeconds")).toDouble();
    const auto middle_offset =
        reframe_suggestions.at(2).toMap().value(QStringLiteral("frameOffsetX")).toDouble();
    if (!auto_reframe_controller.setSelectedClipKeyframeValue(
            QStringLiteral("frameOffsetX"), middle_seconds, middle_offset + 0.4)) {
      return fail("an Auto Reframe position point should be directly editable");
    }
    auto_reframe_controller.undo();
    auto_reframe_controller.undo();
    const auto undone_reframe = auto_reframe_controller.currentClip();
    if (undone_reframe.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fill") ||
        !undone_reframe.value(QStringLiteral("keyframes")).toList().isEmpty() ||
        undone_reframe.value(QStringLiteral("frameMode")) != before_reframe.value(QStringLiteral("frameMode"))) {
      return fail("manual refinement and the complete Auto Reframe application should undo independently");
    }
    auto_reframe_controller.redo();
    if (auto_reframe_controller.currentClip().value(QStringLiteral("keyframes")).toList().size() != 15 ||
        !auto_reframe_controller.saveTimeline()) {
      return fail("Auto Reframe should redo atomically and save its editable motion");
    }
    TimelineDocumentController reopened_auto_reframe_controller;
    if (!reopened_auto_reframe_controller.loadTimeline(
            auto_reframe_controller.savedDocumentPath()) ||
        reopened_auto_reframe_controller.currentClip()
                .value(QStringLiteral("frameMode")).toString() != QStringLiteral("fit") ||
        reopened_auto_reframe_controller.currentClip()
                .value(QStringLiteral("keyframes")).toList().size() != 15) {
      return fail("Auto Reframe transform keyframes should survive save and reopen");
    }
    const auto auto_reframe_render =
        workspace.filePath(QStringLiteral("auto-reframe-render.mp4"));
    if (!reopened_auto_reframe_controller.renderCurrentTimeline(auto_reframe_render)) {
      return fail("Auto Reframe should render through the shared timeline transform graph");
    }
    const auto early_subject_x =
        bright_subject_center_x(ffmpeg, auto_reframe_render, 0.15, 180, 320);
    const auto late_subject_x =
        bright_subject_center_x(ffmpeg, auto_reframe_render, 1.80, 180, 320);
    if (early_subject_x < 0.38 || early_subject_x > 0.62 ||
        late_subject_x < 0.38 || late_subject_x > 0.62) {
      std::cerr << "Auto Reframe rendered subject centers: early=" << early_subject_x
                << " late=" << late_subject_x << '\n';
      return fail("Auto Reframe export should keep the moving subject near horizontal center");
    }

    TimelineDocumentController cancelled_auto_reframe_controller;
    cancelled_auto_reframe_controller.createTimeline(QStringLiteral("Cancelled Auto Reframe"));
    cancelled_auto_reframe_controller.addClipFromGallery(
        QStringLiteral("video:cancelled-moving-subject"), moving_subject_video,
        QStringLiteral("Cancel Subject Analysis"), 2.0, QString{});
    if (!wait_for_media_analysis(&cancelled_auto_reframe_controller)) {
      return fail("cancelled Auto Reframe fixture metadata should resolve");
    }
    qputenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT", QByteArrayLiteral("1"));
    if (!cancelled_auto_reframe_controller.analyzeSelectedClipForAutoReframe()) {
      qunsetenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT");
      return fail("Auto Reframe cancellation fixture should start background analysis");
    }
    cancelled_auto_reframe_controller.cancelAutoReframeAnalysis();
    if (!wait_for_auto_reframe(&cancelled_auto_reframe_controller)) {
      qunsetenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT");
      return fail("Auto Reframe cancellation should finish its bounded background work");
    }
    qunsetenv("PHOTO_EDITOR_TEST_AUTO_REFRAME_LUMA_SUBJECT");
    if (!cancelled_auto_reframe_controller.autoReframeSuggestions().isEmpty() ||
        !cancelled_auto_reframe_controller.currentClip()
             .value(QStringLiteral("keyframes")).toList().isEmpty()) {
      return fail("cancelling Auto Reframe should leave the selected clip untouched");
    }

    const auto stabilization_cache =
        workspace.filePath(QStringLiteral("timeline-stabilization-cache"));
    qputenv("PHOTO_EDITOR_TIMELINE_STABILIZATION_CACHE", stabilization_cache.toUtf8());
    TimelineDocumentController stabilization_controller;
    stabilization_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Stabilization Delivery Truth")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    stabilization_controller.addClipFromGallery(
        QStringLiteral("video:shaky-camera"), shaky_camera_video,
        QStringLiteral("Handheld Source"), 3.0, QString{});
    if (!wait_for_media_analysis(&stabilization_controller) ||
        !stabilization_controller.setSelectedClipStabilization(
            true, 0.65, 0.75, true, 0.35)) {
      return fail("a video clip should accept stabilization and rolling-shutter settings");
    }
    auto stabilization_state = stabilization_controller.currentClip();
    if (!stabilization_state.value(QStringLiteral("stabilizationEnabled")).toBool() ||
        !stabilization_state.value(QStringLiteral("rollingShutterEnabled")).toBool() ||
        !nearly_equal(
            stabilization_state.value(QStringLiteral("stabilizationSmoothing")).toDouble(),
            0.65) ||
        !nearly_equal(
            stabilization_state.value(QStringLiteral("stabilizationCropProtection")).toDouble(),
            0.75) ||
        !nearly_equal(
            stabilization_state.value(QStringLiteral("rollingShutterAmount")).toDouble(),
            0.35)) {
      return fail("stabilization controls should publish their exact bounded values");
    }
    stabilization_controller.undo();
    if (stabilization_controller.currentClip()
            .value(QStringLiteral("stabilizationEnabled")).toBool()) {
      return fail("stabilization settings should undo as one document edit");
    }
    stabilization_controller.redo();
    if (!stabilization_controller.currentClip()
             .value(QStringLiteral("stabilizationEnabled")).toBool()) {
      return fail("stabilization settings should redo atomically");
    }

    double maximum_stabilization_progress = 0.0;
    if (!stabilization_controller.analyzeSelectedClipStabilization() ||
        !stabilization_controller.stabilizationAnalysisBusy() ||
        !wait_for_stabilization(
            &stabilization_controller, &maximum_stabilization_progress)) {
      std::cerr << "stabilization status: "
                << stabilization_controller.stabilizationAnalysisStatus().toStdString()
                << '\n';
      return fail("stabilization should complete its cancellable two-pass analysis");
    }
    stabilization_state = stabilization_controller.currentClip();
    const auto stabilized_preview_path =
        QUrl(stabilization_state
                 .value(QStringLiteral("stabilizationPreviewSourceUrl")).toString())
            .toLocalFile();
    if (maximum_stabilization_progress < 0.50 ||
        !stabilization_state.value(QStringLiteral("stabilizationReady")).toBool() ||
        stabilization_state.value(QStringLiteral("stabilizationAnalysisState")).toString() !=
            QStringLiteral("ready") ||
        !QFileInfo(stabilized_preview_path).isReadable()) {
      return fail("stabilization should expose real progress and a readable Program preview");
    }
    stabilization_controller.setStabilizationCompareBefore(true);
    if (!stabilization_controller.stabilizationCompareBefore()) {
      return fail("a ready stabilized clip should support session-only before/after comparison");
    }
    stabilization_controller.setStabilizationCompareBefore(false);

    if (!stabilization_controller.saveTimeline()) {
      return fail("stabilization settings and cache references should save with the timeline");
    }
    QFile stabilization_document(stabilization_controller.savedDocumentPath());
    if (!stabilization_document.open(QIODevice::ReadOnly)) {
      return fail("the saved stabilization timeline should be readable");
    }
    const auto stabilization_json =
        QJsonDocument::fromJson(stabilization_document.readAll()).object();
    const auto stabilization_clip_json =
        stabilization_json.value(QStringLiteral("tracks")).toArray().at(0).toObject()
            .value(QStringLiteral("clips")).toArray().at(0).toObject();
    const auto stabilization_transform_path =
        stabilization_clip_json.value(QStringLiteral("stabilizationTransformPath")).toString();
    const auto stabilization_render_path =
        stabilization_clip_json.value(QStringLiteral("stabilizationRenderPath")).toString();
    if (!QFileInfo(stabilization_transform_path).isReadable() ||
        !QFileInfo(stabilization_render_path).isReadable() ||
        stabilization_json.contains(QStringLiteral("stabilizationCompareBefore"))) {
      return fail("saved stabilization should retain analyzed artifacts but not comparison UI state");
    }
    QString stabilization_probe_error;
    const auto stabilization_stream = process_stdout(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"),
         QStringLiteral("stream=codec_name,width,height"),
         QStringLiteral("-of"), QStringLiteral("csv=p=0"),
         stabilization_render_path},
        &stabilization_probe_error);
    if (!stabilization_stream.contains(QStringLiteral("ffv1")) ||
        !stabilization_stream.contains(QStringLiteral("320")) ||
        !stabilization_stream.contains(QStringLiteral("240"))) {
      std::cerr << "stabilization cache probe: "
                << stabilization_stream.toStdString() << " "
                << stabilization_probe_error.toStdString() << '\n';
      return fail("final delivery stabilization should use a full-resolution lossless cache");
    }
    QFile first_stabilized_media(stabilization_render_path);
    if (!first_stabilized_media.open(QIODevice::ReadOnly)) {
      return fail("the first stabilized delivery cache should be hashable");
    }
    const auto first_stabilized_hash = QCryptographicHash::hash(
        first_stabilized_media.readAll(), QCryptographicHash::Sha256);

    if (!stabilization_controller.setSelectedClipStabilization(
            true, 0.65, 0.20, true, -0.35)) {
      return fail("stabilization render settings should remain editable after analysis");
    }
    stabilization_state = stabilization_controller.currentClip();
    if (stabilization_state.value(QStringLiteral("stabilizationAnalysisState")).toString() !=
            QStringLiteral("needs-build") ||
        stabilization_state.value(QStringLiteral("stabilizationReady")).toBool() ||
        !stabilization_state
             .value(QStringLiteral("stabilizationPreviewSourceUrl")).toString().isEmpty()) {
      return fail("changing crop or skew should invalidate delivery media while retaining motion analysis");
    }
    double rebuild_progress = 0.0;
    if (!stabilization_controller.analyzeSelectedClipStabilization() ||
        !wait_for_stabilization(&stabilization_controller, &rebuild_progress) ||
        rebuild_progress < 0.92 ||
        !stabilization_controller.currentClip()
             .value(QStringLiteral("stabilizationReady")).toBool()) {
      return fail("changed stabilization settings should rebuild from the reusable motion transform");
    }
    QFile second_stabilized_media(stabilization_render_path);
    if (!second_stabilized_media.open(QIODevice::ReadOnly)) {
      return fail("the rebuilt stabilization cache should be readable");
    }
    const auto second_stabilized_hash = QCryptographicHash::hash(
        second_stabilized_media.readAll(), QCryptographicHash::Sha256);
    if (first_stabilized_hash == second_stabilized_hash) {
      return fail("crop protection and signed rolling-shutter correction should change real pixels");
    }
    if (!stabilization_controller.saveTimeline()) {
      return fail("rebuilt stabilization should persist its final settings");
    }
    TimelineDocumentController reopened_stabilization_controller;
    if (!reopened_stabilization_controller.loadTimeline(
            stabilization_controller.savedDocumentPath())) {
      return fail("a saved stabilized timeline should reopen");
    }
    const auto reopened_stabilization = reopened_stabilization_controller.currentClip();
    if (!reopened_stabilization.value(QStringLiteral("stabilizationReady")).toBool() ||
        !nearly_equal(
            reopened_stabilization
                .value(QStringLiteral("stabilizationCropProtection")).toDouble(),
            0.20) ||
        !nearly_equal(
            reopened_stabilization.value(QStringLiteral("rollingShutterAmount")).toDouble(),
            -0.35)) {
      return fail("stabilization settings and valid cache state should survive save and reopen");
    }
    const auto stabilized_delivery_path =
        workspace.filePath(QStringLiteral("stabilized-delivery.mp4"));
    if (!reopened_stabilization_controller.renderCurrentTimeline(
            stabilized_delivery_path) ||
        QFileInfo(stabilized_delivery_path).size() < 1024) {
      return fail("final timeline export should render from the lossless stabilized source");
    }
    const auto stabilization_cache_summary =
        reopened_stabilization_controller.projectCacheSummary();
    const auto stabilization_analysis_category =
        cache_category(stabilization_cache_summary, QStringLiteral("analysis"));
    const auto stabilization_preview_category =
        cache_category(stabilization_cache_summary, QStringLiteral("preview"));
    if (stabilization_analysis_category.value(QStringLiteral("fileCount")).toInt() < 1 ||
        stabilization_preview_category.value(QStringLiteral("fileCount")).toInt() != 2 ||
        !stabilization_analysis_category.value(QStringLiteral("canClean")).toBool() ||
        !stabilization_preview_category.value(QStringLiteral("canClean")).toBool()) {
      return fail("the cache manager should separate reusable stabilization analysis from Program and delivery previews");
    }
    const auto stabilization_cache_cleanup =
        reopened_stabilization_controller.cleanupProjectCaches({
            QStringLiteral("analysis"), QStringLiteral("preview")});
    const auto stabilization_after_cleanup =
        reopened_stabilization_controller.currentClip();
    if (!stabilization_cache_cleanup.value(QStringLiteral("ok")).toBool() ||
        stabilization_cache_cleanup.value(QStringLiteral("removedFileCount")).toInt() < 3 ||
        QFileInfo(stabilization_transform_path).exists() ||
        QFileInfo(stabilization_render_path).exists() ||
        !QFileInfo(shaky_camera_video).isReadable() ||
        stabilization_after_cleanup
            .value(QStringLiteral("stabilizationReady")).toBool() ||
        stabilization_after_cleanup
                .value(QStringLiteral("stabilizationAnalysisState")).toString() !=
            QStringLiteral("not-analyzed")) {
      return fail("stabilization cache cleanup should invalidate only derived state and preserve the original camera media");
    }
    if (!reopened_stabilization_controller.analyzeSelectedClipStabilization() ||
        !wait_for_stabilization(&reopened_stabilization_controller) ||
        !reopened_stabilization_controller.currentClip()
             .value(QStringLiteral("stabilizationReady")).toBool()) {
      return fail("cleaned stabilization analysis and previews should rebuild through the ordinary Motion workflow");
    }

    TimelineDocumentController cancelled_stabilization_controller;
    cancelled_stabilization_controller.createTimeline(
        QStringLiteral("Cancelled Stabilization"));
    cancelled_stabilization_controller.addClipFromGallery(
        QStringLiteral("video:cancelled-stabilization"), shaky_camera_video,
        QStringLiteral("Cancel Camera Analysis"), 3.0, QString{});
    if (!wait_for_media_analysis(&cancelled_stabilization_controller) ||
        !cancelled_stabilization_controller.setSelectedClipStabilization(
            true, 0.55, 0.50, false, 0.0) ||
        !cancelled_stabilization_controller.analyzeSelectedClipStabilization() ||
        !cancelled_stabilization_controller.stabilizationAnalysisBusy()) {
      return fail("the cancellation proof should start a real stabilization process");
    }
    const auto active_stabilization_jobs =
        cancelled_stabilization_controller.timelineBackgroundJobs();
    auto stabilization_job_id = QString{};
    for (const auto& value : active_stabilization_jobs) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobKind")).toString() ==
          QStringLiteral("stabilization")) {
        stabilization_job_id = job.value(QStringLiteral("jobId")).toString();
        if (!job.value(QStringLiteral("progressKnown")).toBool() ||
            !job.value(QStringLiteral("canCancel")).toBool() ||
            job.value(QStringLiteral("affectedType")).toString() !=
                QStringLiteral("clip")) {
          return fail("active stabilization should publish exact progress and clip routing in the background-job center");
        }
      }
    }
    if (stabilization_job_id.isEmpty() ||
        !cancelled_stabilization_controller.controlTimelineBackgroundJob(
            stabilization_job_id, QStringLiteral("cancel"))) {
      return fail("the background-job center should cancel active stabilization through its real controller path");
    }
    if (!wait_for_stabilization(&cancelled_stabilization_controller) ||
        cancelled_stabilization_controller.stabilizationAnalysisBusy() ||
        cancelled_stabilization_controller.currentClip()
            .value(QStringLiteral("stabilizationReady")).toBool() ||
        !cancelled_stabilization_controller.stabilizationAnalysisStatus()
             .contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
      return fail("cancelling stabilization should stop promptly without publishing partial media");
    }
    qunsetenv("PHOTO_EDITOR_TIMELINE_STABILIZATION_CACHE");

    TimelineDocumentController timecode_multicam_controller;
    timecode_multicam_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Timecode Multicam")},
        {QStringLiteral("frameRate"), 30.0},
    });
    timecode_multicam_controller.addVideoTrack();
    timecode_multicam_controller.addClipFromGallery(
        QStringLiteral("video:timecode-a"), timecode_video_one,
        QStringLiteral("TC A"), 0.0, QString{});
    timecode_multicam_controller.addClipFromGallery(
        QStringLiteral("video:timecode-b"), timecode_video_two,
        QStringLiteral("TC B"), 0.0, QString{});
    timecode_multicam_controller.moveSelectedClipToTrackRow(1, 0.0);
    timecode_multicam_controller.selectClip(0);
    timecode_multicam_controller.toggleClipSelection(1);
    if (timecode_multicam_controller.createMulticamGroupFromSelection(
            QStringLiteral("timecode"), QStringLiteral("Timecode Cameras")).isEmpty()) {
      return fail("embedded camera timecode should create a multicam group");
    }
    const auto timecode_angles = timecode_multicam_controller.multicamGroups()
                                     .constFirst().toMap()
                                     .value(QStringLiteral("angles")).toList();
    if (timecode_angles.size() != 2 ||
        !nearly_equal(timecode_angles.at(0).toMap()
                          .value(QStringLiteral("startSeconds")).toDouble(), 0.0, 0.02) ||
        !nearly_equal(timecode_angles.at(1).toMap()
                          .value(QStringLiteral("startSeconds")).toDouble(), 0.50, 0.02)) {
      return fail("embedded timecode sync should preserve the cameras' half-second offset");
    }

    TimelineDocumentController waveform_multicam_controller;
    waveform_multicam_controller.createTimeline(QStringLiteral("Waveform Multicam"));
    waveform_multicam_controller.addVideoTrack();
    waveform_multicam_controller.addClipFromGallery(
        QStringLiteral("video:waveform-a"), silence_analysis_video,
        QStringLiteral("Wave A"), 0.0, QString{});
    waveform_multicam_controller.addClipFromGallery(
        QStringLiteral("video:waveform-b"), silence_analysis_video,
        QStringLiteral("Wave B"), 0.0, QString{});
    waveform_multicam_controller.moveSelectedClipToTrackRow(1, 0.0);
    waveform_multicam_controller.selectClip(0);
    waveform_multicam_controller.toggleClipSelection(1);
    if (waveform_multicam_controller.createMulticamGroupFromSelection(
            QStringLiteral("waveform"), QStringLiteral("Waveform Cameras")).isEmpty()) {
      return fail("matching decoded audio envelopes should create a waveform-synced multicam group");
    }
    const auto waveform_angles = waveform_multicam_controller.multicamGroups()
                                     .constFirst().toMap()
                                     .value(QStringLiteral("angles")).toList();
    if (waveform_angles.size() != 2 ||
        !nearly_equal(waveform_angles.at(0).toMap()
                          .value(QStringLiteral("startSeconds")).toDouble(),
                      waveform_angles.at(1).toMap()
                          .value(QStringLiteral("startSeconds")).toDouble(), 0.02)) {
      return fail("identical waveforms should align both camera starts to the same frame");
    }

    TimelineDocumentController multicam_controller;
    multicam_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Multicam Truth Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    multicam_controller.addVideoTrack();
    multicam_controller.addClipFromGallery(
        QStringLiteral("video:multicam-red"), render_video_one,
        QStringLiteral("Camera Red"), 0.0, QString{});
    multicam_controller.addClipFromGallery(
        QStringLiteral("video:multicam-blue"), render_video_two,
        QStringLiteral("Camera Blue"), 0.0, QString{});
    multicam_controller.moveSelectedClipToTrackRow(1, 0.0);
    multicam_controller.selectClip(0);
    multicam_controller.seekCurrentClipSeconds(0.20);
    if (!multicam_controller.setSelectedClipMulticamSyncMarkerAtPlayhead()) {
      return fail("multicam marker sync should accept a source-local marker on camera one");
    }
    multicam_controller.selectClip(1);
    multicam_controller.seekCurrentClipSeconds(0.50);
    if (!multicam_controller.setSelectedClipMulticamSyncMarkerAtPlayhead()) {
      return fail("multicam marker sync should accept a source-local marker on camera two");
    }
    multicam_controller.selectClip(0);
    multicam_controller.toggleClipSelection(1);
    const auto multicam_group_id =
        multicam_controller.createMulticamGroupFromSelection(
            QStringLiteral("marker"), QStringLiteral("Interview Cameras"));
    if (multicam_group_id.isEmpty() || multicam_controller.multicamGroups().size() != 1) {
      return fail("two marked camera clips should create one editable multicam group");
    }
    const auto synchronized_angles =
        multicam_controller.multicamGroups().constFirst().toMap()
            .value(QStringLiteral("angles")).toList();
    if (synchronized_angles.size() != 2 ||
        !nearly_equal(
            synchronized_angles.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.30, 0.02) ||
        !nearly_equal(
            synchronized_angles.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.0, 0.02)) {
      if (synchronized_angles.size() == 2) {
        std::cerr << "multicam synchronized starts="
                  << synchronized_angles.at(0).toMap()
                         .value(QStringLiteral("startSeconds")).toDouble()
                  << ","
                  << synchronized_angles.at(1).toMap()
                         .value(QStringLiteral("startSeconds")).toDouble()
                  << '\n';
      }
      return fail("marker sync should align different source marker offsets on one sequence frame");
    }
    if (!multicam_controller.switchMulticamAngle(multicam_group_id, 0, 0.60) ||
        multicam_controller.programClip().value(QStringLiteral("displayLabel")).toString() !=
            QStringLiteral("Camera Red") ||
        !multicam_controller.switchMulticamAngle(multicam_group_id, 1, 0.80) ||
        multicam_controller.programClip().value(QStringLiteral("displayLabel")).toString() !=
            QStringLiteral("Camera Blue")) {
      return fail("multicam angle cuts should immediately drive the Program source");
    }
    if (multicam_controller.activeMulticamGroup()
            .value(QStringLiteral("cuts")).toList().size() != 3) {
      return fail("multicam angle switches should preserve an editable cut list");
    }
    multicam_controller.undo();
    if (multicam_controller.activeMulticamGroup()
            .value(QStringLiteral("cuts")).toList().size() != 2) {
      return fail("undo should remove only the most recent multicam angle cut");
    }
    multicam_controller.redo();
    if (multicam_controller.activeMulticamGroup()
            .value(QStringLiteral("cuts")).toList().size() != 3) {
      return fail("redo should restore the exact multicam angle cut");
    }
    const auto active_angle_preview =
        multicam_controller.multicamAnglePreviewState(multicam_group_id, 0, 0.68);
    const auto unavailable_angle_preview =
        multicam_controller.multicamAnglePreviewState(multicam_group_id, 0, 0.10);
    if (!active_angle_preview.value(QStringLiteral("valid")).toBool() ||
        !active_angle_preview.value(QStringLiteral("available")).toBool() ||
        !active_angle_preview.value(QStringLiteral("active")).toBool() ||
        active_angle_preview.value(QStringLiteral("sourceUrl")).toString().isEmpty() ||
        active_angle_preview.value(QStringLiteral("sourceSeconds")).toDouble() <= 0.0 ||
        !unavailable_angle_preview.value(QStringLiteral("valid")).toBool() ||
        unavailable_angle_preview.value(QStringLiteral("available")).toBool()) {
      return fail("the multicam monitor should receive exact per-angle source state at the Program clock");
    }
    if (!multicam_controller.setMulticamAudioMode(
            multicam_group_id, QStringLiteral("fixed-angle"), 0)) {
      return fail("multicam audio should lock to a chosen camera independently of Program video");
    }
    auto audio_group = multicam_controller.activeMulticamGroup();
    auto audio_angles = audio_group.value(QStringLiteral("angles")).toList();
    if (audio_group.value(QStringLiteral("audioMode")).toString() !=
            QStringLiteral("fixed-angle") ||
        audio_group.value(QStringLiteral("audioAngleIndex")).toInt() != 0 ||
        audio_angles.size() != 2 ||
        !audio_angles.at(0).toMap().value(QStringLiteral("audioActive")).toBool() ||
        audio_angles.at(1).toMap().value(QStringLiteral("audioActive")).toBool()) {
      return fail("a fixed multicam audio angle should remain active while Program video uses another angle");
    }
    if (!multicam_controller.setMulticamAudioMode(
            multicam_group_id, QStringLiteral("mute"), -1)) {
      return fail("multicam camera audio should support an explicit muted mode");
    }
    audio_angles = multicam_controller.activeMulticamGroup()
                       .value(QStringLiteral("angles")).toList();
    if (audio_angles.at(0).toMap().value(QStringLiteral("audioActive")).toBool() ||
        audio_angles.at(1).toMap().value(QStringLiteral("audioActive")).toBool()) {
      return fail("muted multicam camera audio should deactivate every angle");
    }
    if (!multicam_controller.setMulticamAudioMode(
            multicam_group_id, QStringLiteral("follow-video"), -1)) {
      return fail("multicam audio should return to following the Program cut");
    }
    audio_angles = multicam_controller.activeMulticamGroup()
                       .value(QStringLiteral("angles")).toList();
    if (audio_angles.at(0).toMap().value(QStringLiteral("audioActive")).toBool() ||
        !audio_angles.at(1).toMap().value(QStringLiteral("audioActive")).toBool()) {
      return fail("follow-video multicam audio should match the active Program angle");
    }
    if (!multicam_controller.nudgeMulticamCut(multicam_group_id, 0.80, 1)) {
      return fail("an existing multicam decision should move by one exact frame");
    }
    auto nudged_cuts = multicam_controller.activeMulticamGroup()
                           .value(QStringLiteral("cuts")).toList();
    if (nudged_cuts.size() != 3 ||
        !nearly_equal(nudged_cuts.at(2).toMap()
                          .value(QStringLiteral("seconds")).toDouble(),
                      0.80 + 1.0 / 30.0, 0.002)) {
      return fail("multicam cut nudging should be frame-snapped and preserve the decision");
    }
    multicam_controller.undo();
    nudged_cuts = multicam_controller.activeMulticamGroup()
                       .value(QStringLiteral("cuts")).toList();
    if (!nearly_equal(nudged_cuts.at(2).toMap()
                          .value(QStringLiteral("seconds")).toDouble(),
                      0.80, 0.002)) {
      return fail("undo should restore the prior multicam cut frame");
    }
    multicam_controller.redo();
    nudged_cuts = multicam_controller.activeMulticamGroup()
                       .value(QStringLiteral("cuts")).toList();
    if (!nearly_equal(nudged_cuts.at(2).toMap()
                          .value(QStringLiteral("seconds")).toDouble(),
                      0.80 + 1.0 / 30.0, 0.002)) {
      return fail("redo should restore the nudged multicam cut frame");
    }
    const auto multicam_render_path =
        workspace.filePath(QStringLiteral("multicam-angle-cuts.mp4"));
    if (!multicam_controller.renderCurrentTimeline(multicam_render_path)) {
      return fail("multicam angle cuts should render through the shared export graph");
    }
    const auto blue_opening = process_bytes(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
         QStringLiteral("0.10"), QStringLiteral("-i"), multicam_render_path,
         QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
         QStringLiteral("scale=1:1"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    const auto red_middle = process_bytes(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
         QStringLiteral("0.68"), QStringLiteral("-i"), multicam_render_path,
         QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
         QStringLiteral("scale=1:1"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    const auto blue_ending = process_bytes(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"),
         QStringLiteral("0.90"), QStringLiteral("-i"), multicam_render_path,
         QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-vf"),
         QStringLiteral("scale=1:1"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    if (blue_opening.size() < 3 || red_middle.size() < 3 || blue_ending.size() < 3 ||
        static_cast<unsigned char>(blue_opening.at(2)) < 180 ||
        static_cast<unsigned char>(red_middle.at(0)) < 180 ||
        static_cast<unsigned char>(blue_ending.at(2)) < 180) {
      return fail("export should visibly follow blue-red-blue multicam angle cuts");
    }
    const auto red_middle_audio_db = detected_channel_mean_volume_db(
        ffmpeg, multicam_render_path, 0, 0.64, 0.12);
    const auto blue_ending_audio_db = detected_channel_mean_volume_db(
        ffmpeg, multicam_render_path, 0, 0.90, 0.12);
    if (red_middle_audio_db < -60.0 || blue_ending_audio_db > -80.0) {
      return fail("follow-video multicam audio should render the active angle's audio and silence an angle without audio");
    }
    if (!multicam_controller.setMulticamAudioMode(
            multicam_group_id, QStringLiteral("fixed-angle"), 0)) {
      return fail("the saved multicam fixture should retain its chosen audio policy");
    }
    if (!multicam_controller.saveTimeline()) {
      return fail("multicam relationships should persist with the timeline document");
    }
    TimelineDocumentController reopened_multicam_controller;
    if (!reopened_multicam_controller.loadTimeline(multicam_controller.savedDocumentPath()) ||
        reopened_multicam_controller.multicamGroups().size() != 1 ||
        reopened_multicam_controller.multicamGroups().constFirst().toMap()
                .value(QStringLiteral("cuts")).toList().size() != 3 ||
        reopened_multicam_controller.multicamGroups().constFirst().toMap()
                .value(QStringLiteral("audioMode")).toString() !=
            QStringLiteral("fixed-angle") ||
        reopened_multicam_controller.multicamGroups().constFirst().toMap()
                .value(QStringLiteral("audioAngleIndex")).toInt() != 0) {
      return fail("multicam angles, cuts, and audio policy should survive save and reopen");
    }

    TimelineDocumentController preset_controller;
    preset_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Platform Presets")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    preset_controller.addClipFromGallery(
        QStringLiteral("photo:preset"), rotation_image, QStringLiteral("Preset Source"), 0.20, QString{});
    const auto preset_clip_id = preset_controller.currentClip().value(QStringLiteral("clipId")).toString();
    const auto mismatched_delivery = preset_controller.timelineDeliveryPreflight(QStringLiteral("youtube"));
    if (mismatched_delivery.value(QStringLiteral("ok")).toBool() ||
        mismatched_delivery.value(QStringLiteral("errorCount")).toInt() == 0) {
      return fail("delivery preflight should block a platform aspect-ratio mismatch");
    }
    struct PresetExpectation final {
      QString key;
      QString normalized_key;
      int width = 0;
      int height = 0;
    };
    const QVector<PresetExpectation> preset_expectations = {
        {QStringLiteral("youtube"), QStringLiteral("youtube"), 1920, 1080},
        {QStringLiteral("youtube_shorts"), QStringLiteral("youtube_shorts"), 1080, 1920},
        {QStringLiteral("tiktok"), QStringLiteral("tiktok"), 1080, 1920},
        {QStringLiteral("instagram"), QStringLiteral("instagram"), 1080, 1350},
        {QStringLiteral("square"), QStringLiteral("instagram"), 1080, 1080},
    };
    for (int preset_index = 0; preset_index < preset_expectations.size(); ++preset_index) {
      const auto& expectation = preset_expectations.at(preset_index);
      preset_controller.applyOutputPreset(expectation.key);
      if (preset_controller.sequenceWidth() != expectation.width ||
          preset_controller.sequenceHeight() != expectation.height ||
          preset_controller.exportPreset() != expectation.normalized_key ||
          preset_controller.currentClip().value(QStringLiteral("clipId")).toString() != preset_clip_id) {
        return fail("output presets should change format metadata without replacing timeline clips");
      }
      const auto delivery = preset_controller.timelineDeliveryPreflight(expectation.key);
      if (!delivery.value(QStringLiteral("ok")).toBool() ||
          delivery.value(QStringLiteral("errorCount")).toInt() != 0 ||
          delivery.value(QStringLiteral("recommendedWidth")).toInt() != expectation.width ||
          delivery.value(QStringLiteral("recommendedHeight")).toInt() != expectation.height ||
          delivery.value(QStringLiteral("videoBitrateMbps")).toInt() <= 0 ||
          delivery.value(QStringLiteral("audioBitrateKbps")).toInt() <= 0 ||
          delivery.value(QStringLiteral("warningCount")).toInt() == 0) {
        return fail("delivery preflight should validate matching platform format, encode, caption, audio, color, and safe-area state");
      }
      if (preset_index == 0) {
        preset_controller.undo();
        if (preset_controller.sequenceWidth() != 320 || preset_controller.sequenceHeight() != 240) {
          return fail("output format changes should be undoable");
        }
        preset_controller.redo();
      }

      const auto preset_path = workspace.filePath(
          QStringLiteral("preset-%1.mp4").arg(expectation.key));
      if (!preset_controller.renderCurrentTimeline(preset_path)) {
        return fail("every MVP platform and aspect preset should render");
      }
      const auto probe_json = process_stdout(
          ffprobe,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-show_entries"),
              QStringLiteral("stream=codec_type,codec_name,width,height,r_frame_rate,color_space,color_transfer,color_primaries,sample_rate"),
              QStringLiteral("-of"), QStringLiteral("json"),
              preset_path,
          });
      const auto probe_document = QJsonDocument::fromJson(probe_json.toUtf8());
      auto video_valid = false;
      auto audio_valid = false;
      for (const auto& stream_value : probe_document.object().value(QStringLiteral("streams")).toArray()) {
        const auto stream = stream_value.toObject();
        if (stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("video")) {
          video_valid = stream.value(QStringLiteral("codec_name")).toString() == QStringLiteral("h264") &&
                        stream.value(QStringLiteral("width")).toInt() == expectation.width &&
                        stream.value(QStringLiteral("height")).toInt() == expectation.height &&
                        stream.value(QStringLiteral("r_frame_rate")).toString() == QStringLiteral("30/1") &&
                        stream.value(QStringLiteral("color_space")).toString() == QStringLiteral("bt709") &&
                        stream.value(QStringLiteral("color_transfer")).toString() == QStringLiteral("bt709") &&
                        stream.value(QStringLiteral("color_primaries")).toString() == QStringLiteral("bt709");
        } else if (stream.value(QStringLiteral("codec_type")).toString() == QStringLiteral("audio")) {
          audio_valid = stream.value(QStringLiteral("codec_name")).toString() == QStringLiteral("aac") &&
                        stream.value(QStringLiteral("sample_rate")).toString() == QStringLiteral("48000");
        }
      }
      QFile preset_file(preset_path);
      if (!preset_file.open(QIODevice::ReadOnly)) {
        return fail("rendered platform preset should be readable");
      }
      const auto preset_bytes = preset_file.readAll();
      if (!video_valid || !audio_valid || preset_bytes.indexOf("moov") < 0 ||
          preset_bytes.indexOf("mdat") < 0 || preset_bytes.indexOf("moov") > preset_bytes.indexOf("mdat")) {
        std::cerr << "preset validation failed for " << expectation.key.toStdString()
                  << " video=" << video_valid << " audio=" << audio_valid
                  << " moov=" << preset_bytes.indexOf("moov")
                  << " mdat=" << preset_bytes.indexOf("mdat")
                  << " probe=" << probe_json.toStdString() << '\n';
        return fail("platform outputs should have valid H.264/AAC metadata, Rec.709 tags, and fast start");
      }
    }

    TimelineDocumentController loudness_controller;
    loudness_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Post-Mix Loudness")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
        {QStringLiteral("exportPreset"), QStringLiteral("youtube")},
    });
    loudness_controller.addClipFromGallery(
        QStringLiteral("video:loudness"), render_video_one,
        QStringLiteral("Measured Tone"), 1.2, QString{});
    loudness_controller.applyOutputPreset(QStringLiteral("youtube"));
    if (!wait_for_media_analysis(&loudness_controller) ||
        !loudness_controller.analyzeTimelineLoudness() ||
        !wait_for_loudness_analysis(&loudness_controller)) {
      std::cerr << "loudness analysis status: "
                << loudness_controller.loudnessAnalysisStatus().toStdString()
                << " busy=" << loudness_controller.loudnessAnalysisBusy() << '\n';
      return fail("post-mix loudness analysis should finish without blocking the timeline controller");
    }
    const auto loudness_result = loudness_controller.loudnessAnalysisResult();
    const auto measured_lufs = loudness_result.value(QStringLiteral("integratedLufs")).toDouble();
    const auto measured_peak = loudness_result.value(QStringLiteral("truePeakDbtp")).toDouble();
    if (!loudness_result.value(QStringLiteral("measured")).toBool() ||
        measured_lufs < -70.0 || measured_lufs > 0.0 ||
        measured_peak < -70.0 || measured_peak > 1.0 ||
        qAbs(loudness_result.value(QStringLiteral("recommendedGainDb")).toDouble()) <= 0.05) {
      return fail("loudness analysis should expose finite integrated LUFS, true peak, range, and normalization gain");
    }
    const auto measured_delivery = loudness_controller.timelineDeliveryPreflight(QStringLiteral("youtube"));
    auto found_unmeasured_loudness = false;
    for (const auto& issue_value : measured_delivery.value(QStringLiteral("issues")).toList()) {
      if (issue_value.toMap().value(QStringLiteral("key")).toString() ==
          QStringLiteral("loudness-unmeasured")) {
        found_unmeasured_loudness = true;
      }
    }
    if (!measured_delivery.value(QStringLiteral("loudnessMeasured")).toBool() ||
        found_unmeasured_loudness) {
      return fail("delivery preflight should consume measured post-mix loudness instead of retaining the manual warning");
    }
    const auto audio_track_gain = [](const TimelineDocumentController& controller) {
      for (const auto& track_value : controller.sequenceTracks()) {
        const auto track = track_value.toMap();
        if (track.value(QStringLiteral("trackKind")).toString() == QStringLiteral("audio")) {
          return track.value(QStringLiteral("baseAudioGainDb")).toDouble();
        }
      }
      return 0.0;
    };
    const auto gain_before_normalize = audio_track_gain(loudness_controller);
    if (!loudness_controller.normalizeTimelineLoudness()) {
      return fail("measured post-mix audio should support one-step non-destructive normalization");
    }
    const auto gain_after_normalize = audio_track_gain(loudness_controller);
    if (qAbs(gain_after_normalize - gain_before_normalize) <= 0.05 ||
        loudness_controller.loudnessAnalysisResult().value(QStringLiteral("measured")).toBool()) {
      return fail("normalization should adjust all contributing tracks and invalidate the stale measurement");
    }
    loudness_controller.undo();
    if (!nearly_equal(audio_track_gain(loudness_controller), gain_before_normalize, 0.01)) {
      return fail("post-mix normalization should undo as one timeline edit");
    }
    if (!loudness_controller.analyzeTimelineLoudness()) {
      return fail("loudness analysis should be repeatable after an edit");
    }
    loudness_controller.cancelTimelineLoudnessAnalysis();
    if (loudness_controller.loudnessAnalysisBusy() ||
        loudness_controller.loudnessAnalysisResult().value(QStringLiteral("measured")).toBool()) {
      return fail("cancelling loudness analysis should stop immediately without publishing stale values");
    }

    const auto framing_keyframe_count = [&preset_controller]() {
      auto count = 0;
      for (const auto& value :
           preset_controller.currentClip().value(QStringLiteral("keyframes")).toList()) {
        if (value.toMap().value(QStringLiteral("parameter")).toString() ==
            QStringLiteral("frameOffsetY")) {
          ++count;
        }
      }
      return count;
    };
    preset_controller.applyOutputPreset(QStringLiteral("landscape"));
    preset_controller.setSelectedFrameMode(QStringLiteral("fill"));
    preset_controller.setSelectedFrameScale(1.4);
    preset_controller.setSelectedFrameOffsetX(0.30);
    preset_controller.seekSequenceSeconds(0.0);
    preset_controller.toggleSelectedClipKeyframe(QStringLiteral("frameOffsetY"));
    preset_controller.setSelectedFrameOffsetY(0.10);
    preset_controller.seekSequenceSeconds(0.15);
    preset_controller.toggleSelectedClipKeyframe(QStringLiteral("frameOffsetY"));
    preset_controller.setSelectedFrameOffsetY(-0.20);
    if (framing_keyframe_count() != 2) {
      return fail("landscape output variant should retain its own framing keyframes");
    }

    preset_controller.applyOutputPreset(QStringLiteral("vertical"));
    preset_controller.setSelectedFrameScale(2.0);
    preset_controller.setSelectedFrameOffsetX(-0.25);
    preset_controller.toggleSelectedClipKeyframe(QStringLiteral("frameOffsetY"));
    if (framing_keyframe_count() != 1) {
      return fail("a first-visit output variant should inherit framing but remain independently editable");
    }
    preset_controller.applyOutputPreset(QStringLiteral("landscape"));
    if (preset_controller.activeOutputVariant() != QStringLiteral("landscape") ||
        !nearly_equal(
            preset_controller.currentClip().value(QStringLiteral("baseFrameScale")).toDouble(),
            1.4) ||
        !nearly_equal(
            preset_controller.currentClip().value(QStringLiteral("baseFrameOffsetX")).toDouble(),
            0.30) ||
        framing_keyframe_count() != 2) {
      return fail("switching back to 16:9 should restore that format's framing and keyframes");
    }
    preset_controller.undo();
    if (preset_controller.activeOutputVariant() != QStringLiteral("vertical") ||
        !nearly_equal(
            preset_controller.currentClip().value(QStringLiteral("baseFrameScale")).toDouble(),
            2.0) ||
        !nearly_equal(
            preset_controller.currentClip().value(QStringLiteral("baseFrameOffsetX")).toDouble(),
            -0.25) ||
        framing_keyframe_count() != 1) {
      return fail("output variant switching should undo with the target format's independent framing");
    }
    preset_controller.redo();
    if (preset_controller.activeOutputVariant() != QStringLiteral("landscape") ||
        framing_keyframe_count() != 2) {
      return fail("output variant switching should redo atomically");
    }
    if (preset_controller.outputVariants().size() != 4 || !preset_controller.saveTimeline()) {
      return fail("all four output variants and their overrides should be persistable");
    }
    TimelineDocumentController reopened_variant_controller;
    if (!reopened_variant_controller.loadTimeline(preset_controller.savedDocumentPath()) ||
        reopened_variant_controller.activeOutputVariant() != QStringLiteral("landscape") ||
        reopened_variant_controller.outputVariants().size() != 4) {
      return fail("the active output variant should survive save and reopen");
    }
    reopened_variant_controller.applyOutputPreset(QStringLiteral("vertical"));
    if (!nearly_equal(
            reopened_variant_controller.currentClip()
                .value(QStringLiteral("baseFrameScale"))
                .toDouble(),
            2.0) ||
        !nearly_equal(
            reopened_variant_controller.currentClip()
                .value(QStringLiteral("baseFrameOffsetX"))
                .toDouble(),
            -0.25)) {
      return fail("per-format clip framing should survive save and reopen");
    }
    if (!reopened_variant_controller.resetSelectedClipOutputVariantOverride() ||
        !nearly_equal(
            reopened_variant_controller.currentClip()
                .value(QStringLiteral("baseFrameScale"))
                .toDouble(),
            1.0) ||
        !nearly_equal(
            reopened_variant_controller.currentClip()
                .value(QStringLiteral("baseFrameOffsetX"))
                .toDouble(),
            0.0)) {
      return fail("reset should affect only the selected clip in the active output format");
    }
    reopened_variant_controller.applyOutputPreset(QStringLiteral("landscape"));
    if (!nearly_equal(
            reopened_variant_controller.currentClip()
                .value(QStringLiteral("baseFrameScale"))
                .toDouble(),
            1.4)) {
      return fail("resetting vertical framing must not overwrite the landscape variant");
    }

    TimelineDocumentController title_variant_controller;
    title_variant_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Title Output Variants")},
        {QStringLiteral("width"), 1920},
        {QStringLiteral("height"), 1080},
        {QStringLiteral("exportPreset"), QStringLiteral("youtube")},
    });
    title_variant_controller.addTitleClip();
    title_variant_controller.updateSelectedTitle({
        {QStringLiteral("text"), QStringLiteral("Format-aware title")},
        {QStringLiteral("fontSize"), 72.0},
        {QStringLiteral("positionX"), 0.32},
        {QStringLiteral("positionY"), 0.76},
    });
    title_variant_controller.applyOutputPreset(QStringLiteral("vertical"));
    title_variant_controller.updateSelectedTitle({
        {QStringLiteral("fontSize"), 94.0},
        {QStringLiteral("positionX"), 0.58},
        {QStringLiteral("positionY"), 0.68},
    });
    title_variant_controller.applyOutputPreset(QStringLiteral("landscape"));
    if (!nearly_equal(
            title_variant_controller.currentClip()
                .value(QStringLiteral("titleFontSize"))
                .toDouble(),
            72.0) ||
        !nearly_equal(
            title_variant_controller.currentClip()
                .value(QStringLiteral("titlePositionX"))
                .toDouble(),
            0.32) ||
        !nearly_equal(
            title_variant_controller.currentClip()
                .value(QStringLiteral("titlePositionY"))
                .toDouble(),
            0.76) ||
        !title_variant_controller.saveTimeline()) {
      return fail("title size and placement should be independent per output variant");
    }
    TimelineDocumentController reopened_title_variant_controller;
    if (!reopened_title_variant_controller.loadTimeline(
            title_variant_controller.savedDocumentPath())) {
      return fail("format-aware title variants should reopen");
    }
    reopened_title_variant_controller.applyOutputPreset(QStringLiteral("vertical"));
    if (!nearly_equal(
            reopened_title_variant_controller.currentClip()
                .value(QStringLiteral("titleFontSize"))
                .toDouble(),
            94.0) ||
        !nearly_equal(
            reopened_title_variant_controller.currentClip()
                .value(QStringLiteral("titlePositionX"))
                .toDouble(),
            0.58) ||
        !nearly_equal(
            reopened_title_variant_controller.currentClip()
                .value(QStringLiteral("titlePositionY"))
                .toDouble(),
            0.68)) {
      return fail("per-format title layout should survive save and reopen");
    }

    TimelineDocumentController composite_controller;
    composite_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Composite Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    composite_controller.addClipFromGallery(
        QStringLiteral("video:base-red"), render_video_one, QStringLiteral("Red Base"), 0.0, QString{});
    composite_controller.addClipFromGallery(
        QStringLiteral("video:overlay-blue"), render_video_overlay, QStringLiteral("Blue Overlay"), 0.0, QString{});
    composite_controller.addVideoTrack();
    composite_controller.selectClip(1);
    composite_controller.moveSelectedClipToTrackRow(1, 0.0);
    composite_controller.setSelectedClipOpacity(0.5);
    const auto composite_path = workspace.filePath(QStringLiteral("multitrack-composite.mp4"));
    if (!composite_controller.renderCurrentTimeline(composite_path)) {
      return fail("multitrack fixture should render all visible video lanes");
    }
    const auto composite_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.40"),
            QStringLiteral("-i"), composite_path,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (composite_sample.size() < 3) {
      return fail("multitrack verification should sample the composited output");
    }
    const auto composite_red = static_cast<unsigned char>(composite_sample.at(0));
    const auto composite_green = static_cast<unsigned char>(composite_sample.at(1));
    const auto composite_blue = static_cast<unsigned char>(composite_sample.at(2));
    if (composite_red < 80 || composite_red > 190 || composite_green > 45 ||
        composite_blue < 80 || composite_blue > 190) {
      return fail("V2 opacity should blend the blue overlay over the red V1 base");
    }
    if (detected_mean_volume_db(ffmpeg, composite_path, 660) < -45.0 ||
        detected_mean_volume_db(ffmpeg, composite_path, 990) < -45.0) {
      return fail("multitrack export should independently mix audible audio from overlapping lanes");
    }

    composite_controller.toggleTrackMuted(3);
    const auto muted_mix_path = workspace.filePath(QStringLiteral("multitrack-muted-a2.mp4"));
    if (!composite_controller.renderCurrentTimeline(muted_mix_path) ||
        detected_mean_volume_db(ffmpeg, muted_mix_path, 660) < -45.0 ||
        detected_mean_volume_db(ffmpeg, muted_mix_path, 990) > -50.0) {
      return fail("muting A2 should remove only that lane from the exported audio mix");
    }
    composite_controller.toggleTrackMuted(3);
    composite_controller.toggleTrackSolo(3);
    const auto solo_mix_path = workspace.filePath(QStringLiteral("multitrack-solo-a2.mp4"));
    if (!composite_controller.renderCurrentTimeline(solo_mix_path) ||
        detected_mean_volume_db(ffmpeg, solo_mix_path, 990) < -45.0 ||
        detected_mean_volume_db(ffmpeg, solo_mix_path, 660) > -50.0) {
      return fail("soloing A2 should export that lane without unrelated audio tracks");
    }
    composite_controller.toggleTrackSolo(3);

    composite_controller.toggleTrackVisibility(1);
    const auto hidden_overlay_path = workspace.filePath(QStringLiteral("multitrack-hidden-overlay.mp4"));
    if (!composite_controller.renderCurrentTimeline(hidden_overlay_path)) {
      return fail("multitrack fixture should render after hiding an overlay track");
    }
    const auto hidden_overlay_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.40"),
            QStringLiteral("-i"), hidden_overlay_path,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (hidden_overlay_sample.size() < 3 ||
        static_cast<unsigned char>(hidden_overlay_sample.at(0)) < 220 ||
        static_cast<unsigned char>(hidden_overlay_sample.at(2)) > 45) {
      return fail("hidden video tracks should not contribute pixels to timeline export");
    }

    TimelineDocumentController rotation_controller;
    rotation_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Rotation Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    rotation_controller.addClipFromGallery(
        QStringLiteral("photo:rotation"), rotation_image, QStringLiteral("Rotation Source"), 1.0, QString{});
    rotation_controller.setSelectedFrameRotationDegrees(90.0);
    const auto rotation_path = workspace.filePath(QStringLiteral("rotation-output.mp4"));
    if (!rotation_controller.renderCurrentTimeline(rotation_path)) {
      return fail("rotation fixture should render successfully");
    }
    const auto sample_rotation_pixel = [&ffmpeg, &rotation_path](int y) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QStringLiteral("0.20"),
              QStringLiteral("-i"), rotation_path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"), QStringLiteral("crop=2:2:160:%1,scale=1:1,format=rgb24").arg(y),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto rotation_top = sample_rotation_pixel(40);
    const auto rotation_bottom = sample_rotation_pixel(200);
    const auto is_red = [](const QByteArray& sample) {
      return sample.size() >= 3 && static_cast<unsigned char>(sample.at(0)) > 180 &&
             static_cast<unsigned char>(sample.at(2)) < 70;
    };
    const auto is_blue = [](const QByteArray& sample) {
      return sample.size() >= 3 && static_cast<unsigned char>(sample.at(2)) > 180 &&
             static_cast<unsigned char>(sample.at(0)) < 70;
    };
    if (!((is_red(rotation_top) && is_blue(rotation_bottom)) ||
          (is_blue(rotation_top) && is_red(rotation_bottom)))) {
      if (rotation_top.size() >= 3 && rotation_bottom.size() >= 3) {
        std::cerr << "rotation samples top="
                  << static_cast<int>(static_cast<unsigned char>(rotation_top.at(0))) << ','
                  << static_cast<int>(static_cast<unsigned char>(rotation_top.at(1))) << ','
                  << static_cast<int>(static_cast<unsigned char>(rotation_top.at(2))) << " bottom="
                  << static_cast<int>(static_cast<unsigned char>(rotation_bottom.at(0))) << ','
                  << static_cast<int>(static_cast<unsigned char>(rotation_bottom.at(1))) << ','
                  << static_cast<int>(static_cast<unsigned char>(rotation_bottom.at(2))) << '\n';
      }
      return fail("90-degree rotation should turn horizontal red/blue halves into distinct vertical output halves");
    }

    TimelineDocumentController position_controller;
    position_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Position Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    position_controller.addClipFromGallery(
        QStringLiteral("photo:position"), position_image, QStringLiteral("Position Source"), 1.0, QString{});
    position_controller.setSelectedFrameMode(QStringLiteral("stretch"));
    position_controller.setSelectedFrameOffsetX(0.5);
    const auto position_path = workspace.filePath(QStringLiteral("position-output.mp4"));
    if (!position_controller.renderCurrentTimeline(position_path)) {
      return fail("position fixture should render successfully");
    }
    const auto sample_position_pixel = [&ffmpeg, &position_path](int x) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QStringLiteral("0.20"),
              QStringLiteral("-i"), position_path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"),
              QStringLiteral("crop=2:2:%1:120,scale=1:1,format=rgb24").arg(x),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto unmoved_center = sample_position_pixel(120);
    const auto moved_center = sample_position_pixel(200);
    const auto is_white = [](const QByteArray& sample) {
      return sample.size() >= 3 && static_cast<unsigned char>(sample.at(0)) > 210 &&
             static_cast<unsigned char>(sample.at(1)) > 210 &&
             static_cast<unsigned char>(sample.at(2)) > 210;
    };
    const auto is_black = [](const QByteArray& sample) {
      return sample.size() >= 3 && static_cast<unsigned char>(sample.at(0)) < 35 &&
             static_cast<unsigned char>(sample.at(1)) < 35 &&
             static_cast<unsigned char>(sample.at(2)) < 35;
    };
    if (!is_black(unmoved_center) || !is_white(moved_center)) {
      return fail("positive horizontal position should move rendered content right by the Program mapping");
    }

    TimelineDocumentController framing_controller;
    framing_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Framing Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    framing_controller.addClipFromGallery(
        QStringLiteral("photo:framing"), rotation_image, QStringLiteral("Framing Source"), 1.0, QString{});
    const auto fit_path = workspace.filePath(QStringLiteral("fit-output.mp4"));
    if (!framing_controller.renderCurrentTimeline(fit_path)) {
      return fail("fit framing fixture should render successfully");
    }
    framing_controller.setSelectedFrameMode(QStringLiteral("fill"));
    const auto fill_path = workspace.filePath(QStringLiteral("fill-output.mp4"));
    if (!framing_controller.renderCurrentTimeline(fill_path)) {
      return fail("fill framing fixture should render successfully");
    }
    const auto sample_framing_pixel = [&ffmpeg](const QString& path, int y) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QStringLiteral("0.20"),
              QStringLiteral("-i"), path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"),
              QStringLiteral("crop=2:2:80:%1,scale=1:1,format=rgb24").arg(y),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    if (!is_black(sample_framing_pixel(fit_path, 20)) ||
        is_black(sample_framing_pixel(fill_path, 20))) {
      return fail("Fit should letterbox while Fill should crop to occupy the full Program frame");
    }

    TimelineDocumentController effect_render_controller;
    effect_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Effect Render Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    effect_render_controller.addClipFromGallery(
        QStringLiteral("photo:effect-source"),
        rotation_image,
        QStringLiteral("Effect Source"),
        1.0,
        QString{});
    effect_render_controller.addSelectedClipEffect(QStringLiteral("Black & White"));
    const auto effect_render_path = workspace.filePath(QStringLiteral("effect-render-output.mp4"));
    if (!effect_render_controller.renderCurrentTimeline(effect_render_path)) {
      return fail("enabled effect fixture should render successfully");
    }
    const auto sample_effect_pixel = [&ffmpeg](const QString& path) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QStringLiteral("0.20"),
              QStringLiteral("-i"), path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"), QStringLiteral("crop=2:2:80:120,scale=1:1,format=rgb24"),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto monochrome_sample = sample_effect_pixel(effect_render_path);
    if (monochrome_sample.size() < 3 ||
        qAbs(static_cast<int>(static_cast<unsigned char>(monochrome_sample.at(0))) -
             static_cast<int>(static_cast<unsigned char>(monochrome_sample.at(1)))) > 12 ||
        qAbs(static_cast<int>(static_cast<unsigned char>(monochrome_sample.at(1))) -
             static_cast<int>(static_cast<unsigned char>(monochrome_sample.at(2)))) > 12) {
      return fail("Black & White should produce a monochrome exported frame");
    }
    effect_render_controller.setSelectedClipEffectsEnabled(false);
    const auto bypass_render_path = workspace.filePath(QStringLiteral("effect-bypass-output.mp4"));
    if (!effect_render_controller.renderCurrentTimeline(bypass_render_path)) {
      return fail("bypassed effect fixture should render successfully");
    }
    const auto bypass_sample = sample_effect_pixel(bypass_render_path);
    if (bypass_sample.size() < 3 ||
        static_cast<unsigned char>(bypass_sample.at(0)) < 180 ||
        static_cast<unsigned char>(bypass_sample.at(2)) > 70) {
      return fail("stack bypass should restore the unprocessed exported frame");
    }

    TimelineDocumentController adjustment_controller;
    adjustment_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Adjustment Clip Truth")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    adjustment_controller.addClipFromGallery(
        QStringLiteral("video:adjustment-base"), render_video_one,
        QStringLiteral("Red Base"), 0.0, QString{});
    adjustment_controller.seekSequenceSeconds(0.0);
    if (!adjustment_controller.addAdjustmentClip(1.2) ||
        adjustment_controller.currentClip().value(QStringLiteral("mediaType")).toString() !=
            QStringLiteral("adjustment") ||
        adjustment_controller.currentClip().value(QStringLiteral("mediaOnline")).toBool() != true ||
        adjustment_controller.sequenceTracks().size() != 4) {
      return fail("an adjustment clip should be created on a new eligible lane without source media");
    }
    const auto adjustment_clip_id =
        adjustment_controller.currentClip().value(QStringLiteral("clipId")).toString();
    adjustment_controller.undo();
    auto adjustment_exists = [&adjustment_controller, &adjustment_clip_id]() {
      for (const auto& track_value : adjustment_controller.sequenceTracks()) {
        for (const auto& clip_value : track_value.toMap().value(QStringLiteral("clips")).toList()) {
          if (clip_value.toMap().value(QStringLiteral("clipId")).toString() == adjustment_clip_id) {
            return true;
          }
        }
      }
      return false;
    };
    if (adjustment_exists()) {
      return fail("adjustment clip creation should undo in one step");
    }
    adjustment_controller.redo();
    if (!adjustment_exists() ||
        adjustment_controller.currentClip().value(QStringLiteral("mediaType")).toString() !=
            QStringLiteral("adjustment")) {
      return fail("redo should restore the adjustment clip and its selection");
    }
    adjustment_controller.addSelectedClipEffect(QStringLiteral("Black & White"));
    adjustment_controller.addVideoTrack();
    adjustment_controller.addClipFromGallery(
        QStringLiteral("video:adjustment-above"), render_video_two,
        QStringLiteral("Blue Above"), 0.0, QString{});
    if (!adjustment_controller.moveSelectedClipToTrackRowWithMode(
            2, 0.6, QStringLiteral("overwrite"), false)) {
      return fail("the above-adjustment fixture should move to V3");
    }
    const auto adjustment_render_path =
        workspace.filePath(QStringLiteral("adjustment-clip-output.mp4"));
    if (!adjustment_controller.renderCurrentTimeline(adjustment_render_path)) {
      return fail("an adjustment clip should render through the shared composite graph");
    }
    const auto sample_adjustment_pixel = [&ffmpeg](const QString& path, double seconds) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QString::number(seconds, 'f', 3),
              QStringLiteral("-i"), path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"),
              QStringLiteral("crop=2:2:160:120,scale=1:1,format=rgb24"),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto adjusted_below_sample = sample_adjustment_pixel(adjustment_render_path, 0.20);
    const auto unaffected_above_sample = sample_adjustment_pixel(adjustment_render_path, 0.80);
    if (adjusted_below_sample.size() < 3 ||
        qAbs(static_cast<int>(static_cast<unsigned char>(adjusted_below_sample.at(0))) -
             static_cast<int>(static_cast<unsigned char>(adjusted_below_sample.at(1)))) > 12 ||
        qAbs(static_cast<int>(static_cast<unsigned char>(adjusted_below_sample.at(1))) -
             static_cast<int>(static_cast<unsigned char>(adjusted_below_sample.at(2)))) > 12) {
      return fail("an adjustment clip should process the composite of picture lanes beneath it");
    }
    if (unaffected_above_sample.size() < 3 ||
        static_cast<unsigned char>(unaffected_above_sample.at(2)) < 180 ||
        static_cast<unsigned char>(unaffected_above_sample.at(0)) > 70) {
      return fail("picture lanes above an adjustment clip should remain outside its effect scope");
    }
    if (!adjustment_controller.generateRenderCache() ||
        !wait_for_render_cache(&adjustment_controller)) {
      return fail("the exact Program cache should build asynchronously for adjustment clips");
    }
    adjustment_controller.seekSequenceSeconds(0.20);
    const auto adjustment_cache_path =
        QUrl(adjustment_controller.renderCachePlaybackSourceUrl()).toLocalFile();
    const auto cached_adjustment_sample = sample_adjustment_pixel(adjustment_cache_path, 0.20);
    if (cached_adjustment_sample.size() < 3 || adjusted_below_sample.size() < 3) {
      return fail("the adjustment Program cache should expose a readable rendered frame");
    }
    for (auto channel = 0; channel < 3; ++channel) {
      if (qAbs(static_cast<int>(static_cast<unsigned char>(cached_adjustment_sample.at(channel))) -
               static_cast<int>(static_cast<unsigned char>(adjusted_below_sample.at(channel)))) > 8) {
        return fail("Program cache and final export should agree on adjustment pixels");
      }
    }
    if (!adjustment_controller.saveTimeline()) {
      return fail("the adjustment timeline should save");
    }
    TimelineDocumentController reopened_adjustment_controller;
    if (!reopened_adjustment_controller.loadTimeline(
            adjustment_controller.savedDocumentPath())) {
      return fail("the adjustment timeline should reopen");
    }
    auto reopened_adjustment_found = false;
    for (const auto& track_value : reopened_adjustment_controller.sequenceTracks()) {
      for (const auto& clip_value : track_value.toMap().value(QStringLiteral("clips")).toList()) {
        const auto clip = clip_value.toMap();
        if (clip.value(QStringLiteral("clipId")).toString() == adjustment_clip_id &&
            clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("adjustment") &&
            clip.value(QStringLiteral("effectStack")).toList().contains(
                QStringLiteral("Black & White"))) {
          reopened_adjustment_found = true;
        }
      }
    }
    if (!reopened_adjustment_found) {
      return fail("adjustment media type, range, and effect stack should survive save and reopen");
    }

    TimelineDocumentController dissolve_controller;
    dissolve_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Cross Dissolve Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    dissolve_controller.addClipFromGallery(
        QStringLiteral("video:dissolve-red"), render_video_one, QStringLiteral("Red"), 0.0, QString{});
    dissolve_controller.addClipFromGallery(
        QStringLiteral("video:dissolve-blue"), render_video_two, QStringLiteral("Blue"), 0.0, QString{});
    dissolve_controller.selectClip(1);
    dissolve_controller.setSelectedTrimStartSeconds(0.40);
    dissolve_controller.assignSelectedClipTransition(
        QStringLiteral("in"), QStringLiteral("Cross Dissolve"), 0.40);
    if (dissolve_controller.currentClip().value(QStringLiteral("transitionInName")).toString() !=
            QStringLiteral("Cross Dissolve") ||
        dissolve_controller.clips().at(0).toMap().value(QStringLiteral("transitionOutName")).toString() !=
            QStringLiteral("Cross Dissolve")) {
      return fail("Cross Dissolve should pair the outgoing and incoming edges");
    }
    dissolve_controller.undo();
    if (!dissolve_controller.currentClip().value(QStringLiteral("transitionInName")).toString().isEmpty() ||
        !dissolve_controller.clips().at(0).toMap().value(QStringLiteral("transitionOutName")).toString().isEmpty()) {
      return fail("undo should clear both paired Cross Dissolve edges");
    }
    const auto incoming_start =
        dissolve_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble();
    if (!dissolve_controller.transitionPreviewStatesAt(incoming_start - 0.20).isEmpty()) {
      return fail("undo should also remove the controller-owned transition preview state");
    }
    dissolve_controller.redo();

    const auto dissolve_clips = dissolve_controller.clips();
    const auto outgoing_id =
        dissolve_clips.at(0).toMap().value(QStringLiteral("clipId")).toString();
    const auto incoming_id =
        dissolve_clips.at(1).toMap().value(QStringLiteral("clipId")).toString();
    const auto transition_state_at = [&dissolve_controller](double seconds) {
      const auto states = dissolve_controller.transitionPreviewStatesAt(seconds);
      return states.size() == 1 ? states.constFirst().toMap() : QVariantMap{};
    };
    const auto preview_start = transition_state_at(incoming_start - 0.40);
    const auto preview_midpoint = transition_state_at(incoming_start - 0.20);
    const auto preview_end = transition_state_at(incoming_start);
    if (preview_start.isEmpty() || preview_midpoint.isEmpty() || preview_end.isEmpty() ||
        preview_midpoint.value(QStringLiteral("name")).toString() != QStringLiteral("Cross Dissolve") ||
        preview_midpoint.value(QStringLiteral("outgoingClipId")).toString() != outgoing_id ||
        preview_midpoint.value(QStringLiteral("incomingClipId")).toString() != incoming_id ||
        preview_midpoint.value(QStringLiteral("compositing")).toString() !=
            QStringLiteral("source-over-linear-alpha")) {
      return fail("Cross Dissolve preview should expose one paired controller-owned transition state");
    }
    if (qAbs(preview_start.value(QStringLiteral("progress")).toDouble()) > 0.001 ||
        qAbs(preview_midpoint.value(QStringLiteral("progress")).toDouble() - 0.5) > 0.001 ||
        qAbs(preview_end.value(QStringLiteral("progress")).toDouble() - 1.0) > 0.001 ||
        qAbs(preview_start.value(QStringLiteral("outgoingWeight")).toDouble() - 1.0) > 0.001 ||
        qAbs(preview_midpoint.value(QStringLiteral("incomingAlpha")).toDouble() - 0.5) > 0.001 ||
        qAbs(preview_end.value(QStringLiteral("incomingWeight")).toDouble() - 1.0) > 0.001) {
      return fail("Cross Dissolve preview weights should progress linearly at start, midpoint, and end");
    }
    if (qAbs(preview_start.value(QStringLiteral("incomingSourceSeconds")).toDouble()) > 0.01 ||
        qAbs(preview_midpoint.value(QStringLiteral("incomingSourceSeconds")).toDouble() - 0.20) > 0.01 ||
        qAbs(preview_end.value(QStringLiteral("incomingSourceSeconds")).toDouble() - 0.40) > 0.01) {
      return fail("Cross Dissolve preview should consume the incoming source handle at exact source times");
    }
    if (!dissolve_controller.saveTimeline()) {
      return fail("Cross Dissolve fixture should save its paired transition state");
    }
    TimelineDocumentController reopened_dissolve_controller;
    if (!reopened_dissolve_controller.loadTimeline(dissolve_controller.savedDocumentPath())) {
      return fail("saved Cross Dissolve fixture should reopen");
    }
    const auto reopened_transition_states =
        reopened_dissolve_controller.transitionPreviewStatesAt(incoming_start - 0.20);
    if (reopened_transition_states.size() != 1 ||
        qAbs(reopened_transition_states.constFirst().toMap()
                 .value(QStringLiteral("progress")).toDouble() - 0.5) > 0.001) {
      return fail("Cross Dissolve pairing and preview timing should survive save and reopen");
    }

    const auto dissolve_path = workspace.filePath(QStringLiteral("cross-dissolve-output.mp4"));
    if (!dissolve_controller.renderCurrentTimeline(dissolve_path)) {
      return fail("Cross Dissolve fixture should render successfully");
    }
    const auto sample_dissolve = [&ffmpeg, &dissolve_path](double seconds) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-ss"), QString::number(seconds, 'f', 3),
              QStringLiteral("-i"), dissolve_path,
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
              QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto dissolve_start = sample_dissolve(0.82);
    const auto dissolve_midpoint = sample_dissolve(1.00);
    const auto dissolve_end = sample_dissolve(1.18);
    if (dissolve_start.size() < 3 || dissolve_midpoint.size() < 3 || dissolve_end.size() < 3) {
      return fail("Cross Dissolve verification should sample start, midpoint, and end frames");
    }
    const auto dissolve_red = [](const QByteArray& sample) {
      return static_cast<int>(static_cast<unsigned char>(sample.at(0)));
    };
    const auto dissolve_blue = [](const QByteArray& sample) {
      return static_cast<int>(static_cast<unsigned char>(sample.at(2)));
    };
    if (dissolve_red(dissolve_start) <= dissolve_blue(dissolve_start) + 80 ||
        qAbs(dissolve_red(dissolve_midpoint) - dissolve_blue(dissolve_midpoint)) > 65 ||
        dissolve_blue(dissolve_end) <= dissolve_red(dissolve_end) + 80) {
      return fail("Cross Dissolve pixels should progress from outgoing through a balanced blend to incoming");
    }

    TimelineDocumentController title_controller;
    title_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Title Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    if (title_controller.titleTemplates().size() != 6) {
      return fail("the title library should expose all six low-density production templates");
    }
    title_controller.addClipFromGallery(
        QStringLiteral("video:title-base"), render_video_one, QStringLiteral("Title Base"), 0.0, QString{});
    title_controller.seekSequenceSeconds(0.10);
    if (!title_controller.addTitleClipFromTemplate(QStringLiteral("lower-third"))) {
      return fail("a lower-third template should create a title clip");
    }
    if (title_controller.currentClip().value(QStringLiteral("titleTemplateKey")).toString() !=
            QStringLiteral("lower-third") ||
        title_controller.currentClip().value(QStringLiteral("titleText")).toString() !=
            QStringLiteral("Name · Role") ||
        title_controller.currentClip().value(QStringLiteral("titleAlignment")).toString() !=
            QStringLiteral("left") ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titleFontSize")).toDouble(), 48.0) ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titlePositionX")).toDouble(), 0.10) ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titlePositionY")).toDouble(), 0.82)) {
      return fail("the lower-third template should set its text, type, alignment, and title-safe position");
    }
    title_controller.updateSelectedTitle({
        {QStringLiteral("text"), QStringLiteral("MVP TITLE")},
        {QStringLiteral("fontFamily"), QStringLiteral("Definitely Missing Font Family")},
        {QStringLiteral("fontSize"), 72.0},
        {QStringLiteral("color"), QStringLiteral("#ffffffff")},
        {QStringLiteral("backgroundColor"), QStringLiteral("#d0000000")},
        {QStringLiteral("alignment"), QStringLiteral("center")},
        {QStringLiteral("positionX"), 0.5},
        {QStringLiteral("positionY"), 0.5},
    });
    if (title_controller.currentClip().value(QStringLiteral("titleTemplateKey")).toString() !=
        QStringLiteral("custom")) {
      return fail("manual title styling should identify the clip as customized");
    }
    if (!title_controller.applySelectedTitleTemplate(QStringLiteral("callout"))) {
      return fail("an existing title should accept another production template");
    }
    if (title_controller.currentClip().value(QStringLiteral("mediaType")).toString() != QStringLiteral("title") ||
        title_controller.currentClip().value(QStringLiteral("titleText")).toString() != QStringLiteral("MVP TITLE") ||
        title_controller.currentClip().value(QStringLiteral("titleTemplateKey")).toString() !=
            QStringLiteral("callout") ||
        title_controller.currentClip().value(QStringLiteral("titleFontFamily")).toString() !=
            QStringLiteral("Definitely Missing Font Family") ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titlePositionX")).toDouble(), 0.68) ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titlePositionY")).toDouble(), 0.35)) {
      return fail("applying a title template should preserve authored text while updating its reusable style");
    }
    title_controller.undo();
    if (title_controller.currentClip().value(QStringLiteral("titleTemplateKey")).toString() !=
            QStringLiteral("custom") ||
        !nearly_equal(title_controller.currentClip().value(QStringLiteral("titlePositionX")).toDouble(), 0.5)) {
      return fail("undo should restore the complete custom title style in one step");
    }
    title_controller.redo();
    if (!title_controller.saveTimeline()) {
      return fail("title fixture should save and enqueue a render");
    }
    const auto title_job_id =
        QStringLiteral("timeline-render-%1").arg(title_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(title_job_id)) {
      return fail("title fixture render should complete through the shared job runner");
    }
    TimelineDocumentController reopened_title_controller;
    if (!reopened_title_controller.loadTimeline(title_controller.savedDocumentPath())) {
      return fail("saved title fixture should reopen");
    }
    auto reopened_title_found = false;
    for (const auto& clip_value : reopened_title_controller.clips()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("mediaType")).toString() == QStringLiteral("title") &&
          clip.value(QStringLiteral("titleText")).toString() == QStringLiteral("MVP TITLE") &&
          clip.value(QStringLiteral("titleTemplateKey")).toString() == QStringLiteral("callout") &&
          nearly_equal(clip.value(QStringLiteral("titlePositionX")).toDouble(), 0.68) &&
          nearly_equal(clip.value(QStringLiteral("titlePositionY")).toDouble(), 0.35)) {
        reopened_title_found = true;
        break;
      }
    }
    if (!reopened_title_found) {
      return fail("title text, reusable template identity, style, and position should survive save and reopen");
    }
    const auto title_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.50"),
            QStringLiteral("-i"), title_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("crop=180:110:70:65,scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (title_sample.size() < 3 || static_cast<unsigned char>(title_sample.at(0)) > 225) {
      return fail("exported title pixels should visibly overlay the underlying red video");
    }

    TimelineDocumentController caption_controller;
    caption_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Caption Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    caption_controller.addClipFromGallery(
        QStringLiteral("video:caption-base"), render_video_one, QStringLiteral("Caption Base"), 0.0, QString{});
    caption_controller.generateCaptionsFromTranscript(
        QStringLiteral("One two three. Four five six seven eight nine."));
    const auto caption_count = [&caption_controller]() {
      auto count = 0;
      for (const auto& clip_value : caption_controller.clips()) {
        if (clip_value.toMap().value(QStringLiteral("mediaType")).toString() == QStringLiteral("caption")) {
          ++count;
        }
      }
      return count;
    };
    if (caption_count() != 2 ||
        caption_controller.captionCount() != 2 ||
        caption_controller.trackCount() != 3 ||
        caption_controller.videoTrackCount() != 1 ||
        caption_controller.audioTrackCount() != 1 ||
        !nearly_equal(caption_controller.currentClip().value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
        !nearly_equal(caption_controller.currentClip().value(QStringLiteral("trimmedDurationSeconds")).toDouble(), 0.6) ||
        caption_controller.currentClip().value(QStringLiteral("captionWords")).toList().size() != 3) {
      return fail("transcript generation should create deterministic word-timed captions on one compact CC track");
    }
    if (caption_controller.programClip().value(QStringLiteral("mediaType")).toString() !=
            QStringLiteral("video") ||
        caption_controller.originalPlaybackSourceUrl().isEmpty()) {
      return fail("selecting a caption should preserve the active picture as the Program playback source");
    }
    auto found_caption_track = false;
    for (const auto& track_value : caption_controller.sequenceTracks()) {
      const auto track = track_value.toMap();
      found_caption_track = found_caption_track ||
                            (track.value(QStringLiteral("isCaptionTrack")).toBool() &&
                             track.value(QStringLiteral("trackRole")).toString() == QStringLiteral("caption") &&
                             track.value(QStringLiteral("displayLabel")).toString() == QStringLiteral("CC"));
    }
    if (!found_caption_track) {
      return fail("caption items should live on a semantically dedicated CC track without a paired empty audio row");
    }
    caption_controller.undo();
    if (caption_count() != 0) {
      return fail("caption generation should undo as one edit");
    }
    caption_controller.redo();
    const auto caption_templates = caption_controller.captionStyleTemplates();
    const auto bold_caption_preview =
        caption_controller.captionStyleTemplatePreview(QStringLiteral("bold-social"));
    const auto bold_caption_style = bold_caption_preview.value(QStringLiteral("style")).toMap();
    const auto bold_aspect_overrides =
        bold_caption_preview.value(QStringLiteral("aspectOverrides")).toMap();
    if (caption_templates.size() < 4 ||
        !bold_caption_preview.value(QStringLiteral("found")).toBool() ||
        bold_caption_style.value(QStringLiteral("casing")).toString() !=
            QStringLiteral("upper") ||
        !bold_caption_style.value(QStringLiteral("shadowEnabled")).toBool() ||
        !nearly_equal(
            bold_caption_style.value(QStringLiteral("strokeWidth")).toDouble(), 3.0) ||
        bold_aspect_overrides.size() != 4 ||
        !nearly_equal(
            bold_aspect_overrides.value(QStringLiteral("vertical"))
                .toMap()
                .value(QStringLiteral("safeMarginY"))
                .toDouble(),
            0.16)) {
      return fail("caption templates should expose complete typography, edge treatment, and four aspect-specific overrides");
    }
    if (!caption_controller.applyCaptionStyleTemplate(QStringLiteral("bold-social"), true)) {
      return fail("a reusable caption style should apply to every caption in one operation");
    }
    const auto all_captions_have_style = [&caption_controller](
                                               const QString& template_id,
                                               const QString& casing,
                                               double stroke_width,
                                               bool shadow_enabled) {
      auto found = 0;
      for (const auto& clip_value : caption_controller.clips()) {
        const auto clip = clip_value.toMap();
        if (clip.value(QStringLiteral("mediaType")).toString() != QStringLiteral("caption")) {
          continue;
        }
        ++found;
        if (clip.value(QStringLiteral("captionStyleTemplateId")).toString() != template_id ||
            clip.value(QStringLiteral("captionCasing")).toString() != casing ||
            !nearly_equal(
                clip.value(QStringLiteral("captionStrokeWidth")).toDouble(), stroke_width) ||
            clip.value(QStringLiteral("captionShadowEnabled")).toBool() != shadow_enabled) {
          return false;
        }
      }
      return found == caption_controller.captionCount();
    };
    if (!all_captions_have_style(QStringLiteral("bold-social"), QStringLiteral("upper"), 3.0, true) ||
        !nearly_equal(
            caption_controller.currentClip().value(QStringLiteral("titleFontSize")).toDouble(),
            68.0) ||
        !nearly_equal(
            caption_controller.currentClip().value(QStringLiteral("titlePositionY")).toDouble(),
            0.76)) {
      return fail("applying a caption style should use the active format's exact treatment on every cue");
    }
    caption_controller.undo();
    if (!all_captions_have_style(QStringLiteral("clean-subtitle"), QStringLiteral("none"), 0.0, false)) {
      return fail("applying a caption style to all cues should undo atomically");
    }
    caption_controller.redo();
    if (!all_captions_have_style(QStringLiteral("bold-social"), QStringLiteral("upper"), 3.0, true)) {
      return fail("caption style application should redo deterministically");
    }

    caption_controller.updateSelectedTitle({
        {QStringLiteral("fontFamily"), QStringLiteral("Monospace")},
        {QStringLiteral("fontSize"), 71.0},
        {QStringLiteral("casing"), QStringLiteral("title")},
        {QStringLiteral("strokeColor"), QStringLiteral("#ff102030")},
        {QStringLiteral("strokeWidth"), 2.0},
        {QStringLiteral("shadowEnabled"), true},
        {QStringLiteral("shadowColor"), QStringLiteral("#aa000000")},
        {QStringLiteral("shadowOffsetX"), 4.0},
        {QStringLiteral("shadowOffsetY"), 5.0},
        {QStringLiteral("backgroundColor"), QStringLiteral("#dd202830")},
        {QStringLiteral("backgroundPadding"), 17.0},
        {QStringLiteral("backgroundRadius"), 11.0},
        {QStringLiteral("positionX"), 0.48},
        {QStringLiteral("positionY"), 0.72},
        {QStringLiteral("safeMarginX"), 0.11},
        {QStringLiteral("safeMarginY"), 0.13},
    });
    if (!caption_controller.saveSelectedCaptionStyleTemplate(QStringLiteral("Field Notes"))) {
      return fail("a customized caption should save as a reusable user template");
    }
    auto custom_template_id = QString{};
    for (const auto& value : caption_controller.captionStyleTemplates()) {
      const auto style_template = value.toMap();
      if (style_template.value(QStringLiteral("name")).toString() == QStringLiteral("Field Notes")) {
        custom_template_id = style_template.value(QStringLiteral("templateId")).toString();
      }
    }
    TimelineDocumentController reused_caption_style_controller;
    const auto reused_preview =
        reused_caption_style_controller.captionStyleTemplatePreview(custom_template_id);
    if (custom_template_id.isEmpty() ||
        !reused_preview.value(QStringLiteral("found")).toBool() ||
        reused_preview.value(QStringLiteral("builtIn")).toBool() ||
        reused_preview.value(QStringLiteral("style"))
                .toMap()
                .value(QStringLiteral("fontFamily"))
                .toString() != QStringLiteral("Monospace")) {
      return fail("custom caption styles should load from the atomic user-global store in a new controller");
    }
    reused_caption_style_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Reusable Caption Style")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    reused_caption_style_controller.addClipFromGallery(
        QStringLiteral("video:caption-style-reuse"), render_video_one,
        QStringLiteral("Caption Style Reuse"), 0.0, QString{});
    reused_caption_style_controller.generateCaptionsFromTranscript(
        QStringLiteral("Reusable style proof."));
    if (!reused_caption_style_controller.applyCaptionStyleTemplate(custom_template_id, true) ||
        reused_caption_style_controller.currentClip()
                .value(QStringLiteral("titleFontFamily"))
                .toString() != QStringLiteral("Monospace") ||
        !nearly_equal(
            reused_caption_style_controller.currentClip()
                .value(QStringLiteral("captionBackgroundRadius"))
                .toDouble(),
            11.0)) {
      return fail("a custom caption style should remain reusable in another timeline");
    }
    if (!reused_caption_style_controller.deleteCaptionStyleTemplate(custom_template_id) ||
        TimelineDocumentController{}.captionStyleTemplatePreview(custom_template_id)
            .value(QStringLiteral("found"))
            .toBool()) {
      return fail("custom caption templates should delete without changing built-in styles");
    }
    if (!caption_controller.applyCaptionStyleTemplate(QStringLiteral("boxed-explainer"), true) ||
        !all_captions_have_style(QStringLiteral("boxed-explainer"), QStringLiteral("none"), 0.0, true)) {
      return fail("a second built-in style should replace the full caption treatment cleanly");
    }
    const QStringList caption_emphasis_modes = {
        QStringLiteral("none"), QStringLiteral("highlight"), QStringLiteral("karaoke"),
        QStringLiteral("pop"), QStringLiteral("bounce"), QStringLiteral("single-word"),
    };
    for (const auto& mode : caption_emphasis_modes) {
      if (!caption_controller.setSelectedCaptionEmphasis(mode, false, false) ||
          caption_controller.currentClip()
                  .value(QStringLiteral("captionEmphasisMode"))
                  .toString() != mode) {
        return fail("every supported caption word-emphasis mode should be editable on the selected cue");
      }
    }
    if (!caption_controller.setSelectedCaptionEmphasis(QStringLiteral("bounce"), true, true)) {
      return fail("word emphasis and reduced-motion delivery should apply to all captions atomically");
    }
    const auto all_captions_have_emphasis = [&caption_controller](
                                                  const QString& mode,
                                                  bool reduced_motion) {
      auto found = 0;
      for (const auto& clip_value : caption_controller.clips()) {
        const auto clip = clip_value.toMap();
        if (clip.value(QStringLiteral("mediaType")).toString() != QStringLiteral("caption")) {
          continue;
        }
        ++found;
        if (clip.value(QStringLiteral("captionEmphasisMode")).toString() != mode ||
            clip.value(QStringLiteral("captionEmphasisReducedMotion")).toBool() !=
                reduced_motion) {
          return false;
        }
      }
      return found == caption_controller.captionCount();
    };
    if (!all_captions_have_emphasis(QStringLiteral("bounce"), true)) {
      return fail("all-caption word emphasis should publish one consistent reduced-motion state");
    }
    caption_controller.undo();
    if (caption_controller.currentClip()
            .value(QStringLiteral("captionEmphasisMode"))
            .toString() != QStringLiteral("single-word") ||
        caption_controller.currentClip()
            .value(QStringLiteral("captionEmphasisReducedMotion"))
            .toBool()) {
      return fail("all-caption word emphasis should undo as one edit");
    }
    caption_controller.redo();
    if (!all_captions_have_emphasis(QStringLiteral("bounce"), true) ||
        !caption_controller.setSelectedCaptionEmphasis(QStringLiteral("karaoke"), false, true)) {
      return fail("caption word emphasis should redo and remain replaceable in one operation");
    }
    if (!caption_controller.updateSelectedCaptionWord(1, QStringLiteral("TWO"), 0.22, 0.38)) {
      return fail("caption words should support bounded text and local timing edits");
    }
    const auto edited_words = caption_controller.currentClip().value(QStringLiteral("captionWords")).toList();
    if (edited_words.at(1).toMap().value(QStringLiteral("text")).toString() != QStringLiteral("TWO") ||
        !nearly_equal(edited_words.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(), 0.22) ||
        !caption_controller.currentClip().value(QStringLiteral("titleText")).toString().contains(QStringLiteral("TWO"))) {
      return fail("word edits should immediately rebuild the visible caption text and timing model");
    }
    caption_controller.undo();
    if (caption_controller.currentClip().value(QStringLiteral("titleText")).toString().contains(QStringLiteral("TWO"))) {
      return fail("caption word edits should undo atomically");
    }
    caption_controller.redo();
    caption_controller.updateSelectedTitle({{QStringLiteral("text"), QStringLiteral("Corrected caption.")}});
    if (!caption_controller.mergeSelectedCaptionWithNext() || caption_count() != 1) {
      return fail("adjacent caption clips should merge into one editable caption");
    }
    caption_controller.undo();
    if (caption_count() != 2 || !caption_controller.splitSelectedClip(0.30) || caption_count() != 3 ||
        !caption_controller.mergeSelectedCaptionWithNext() || caption_count() != 2) {
      return fail("caption clips should support split, merge, and undo timing edits");
    }
    if (!caption_controller.saveTimeline()) {
      return fail("caption fixture should save and enqueue a burn-in render");
    }
    if (!caption_controller.exportCaptionSidecar(QStringLiteral("srt")) ||
        !caption_controller.exportCaptionSidecar(QStringLiteral("vtt"))) {
      return fail("caption delivery should export both SRT and WebVTT sidecars");
    }
    const QFileInfo caption_render_info(caption_controller.savedRenderPath());
    const auto caption_srt_path = QDir(caption_render_info.absolutePath())
                                      .filePath(caption_render_info.completeBaseName() + QStringLiteral(".srt"));
    const auto caption_vtt_path = QDir(caption_render_info.absolutePath())
                                      .filePath(caption_render_info.completeBaseName() + QStringLiteral(".vtt"));
    QFile caption_srt_file(caption_srt_path);
    QFile caption_vtt_file(caption_vtt_path);
    if (!caption_srt_file.open(QIODevice::ReadOnly) || !caption_vtt_file.open(QIODevice::ReadOnly)) {
      return fail("caption sidecar files should be readable beside the saved render");
    }
    const auto caption_srt = QString::fromUtf8(caption_srt_file.readAll());
    const auto caption_vtt = QString::fromUtf8(caption_vtt_file.readAll());
    if (!caption_srt.startsWith(QStringLiteral("1\n00:00:00,000 --> ")) ||
        !caption_srt.contains(QStringLiteral("Corrected")) ||
        !caption_srt.contains(QStringLiteral("caption.")) ||
        !caption_vtt.startsWith(QStringLiteral("WEBVTT\n\n00:00:00.000 --> ")) ||
        !caption_vtt.contains(QStringLiteral("Corrected")) ||
        !caption_vtt.contains(QStringLiteral("caption."))) {
      return fail("SRT and WebVTT exports should preserve ordered cue timing and corrected caption text");
    }
    const auto caption_job_id =
        QStringLiteral("timeline-render-%1").arg(caption_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(caption_job_id)) {
      return fail("caption burn-in render should complete through the shared job runner");
    }
    TimelineDocumentController reopened_caption_controller;
    if (!reopened_caption_controller.loadTimeline(caption_controller.savedDocumentPath())) {
      return fail("saved caption fixture should reopen");
    }
    auto reopened_caption_count = 0;
    QStringList reopened_caption_texts;
    for (const auto& clip_value : reopened_caption_controller.clips()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("mediaType")).toString() != QStringLiteral("caption")) {
        continue;
      }
      ++reopened_caption_count;
      reopened_caption_texts.push_back(clip.value(QStringLiteral("titleText")).toString());
    }
    auto reopened_caption_track = false;
    for (const auto& track_value : reopened_caption_controller.sequenceTracks()) {
      reopened_caption_track = reopened_caption_track ||
                               track_value.toMap().value(QStringLiteral("isCaptionTrack")).toBool();
    }
    if (reopened_caption_count != 2 ||
        !reopened_caption_texts.join(QLatin1Char(' ')).contains(QStringLiteral("Corrected caption.")) ||
        !reopened_caption_track ||
        reopened_caption_controller.audioTrackCount() != 1) {
      return fail("caption words, dedicated track semantics, timing, and corrected text should survive save and reopen");
    }
    for (const auto& clip_value : reopened_caption_controller.clips()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("mediaType")).toString() != QStringLiteral("caption")) {
        continue;
      }
      if (clip.value(QStringLiteral("captionStyleTemplateId")).toString() !=
              QStringLiteral("boxed-explainer") ||
          clip.value(QStringLiteral("titleBackgroundColor")).toString() !=
              QStringLiteral("#e61a2027") ||
          !clip.value(QStringLiteral("captionShadowEnabled")).toBool() ||
          clip.value(QStringLiteral("captionEmphasisMode")).toString() !=
              QStringLiteral("karaoke") ||
          clip.value(QStringLiteral("captionEmphasisReducedMotion")).toBool() ||
          !nearly_equal(
              clip.value(QStringLiteral("captionBackgroundPadding")).toDouble(), 18.0) ||
          !nearly_equal(
              clip.value(QStringLiteral("captionBackgroundRadius")).toDouble(), 12.0)) {
        return fail("caption template identity and full visual treatment should survive save and reopen");
      }
    }
    for (int clip_index = 0; clip_index < reopened_caption_controller.clips().size(); ++clip_index) {
      if (reopened_caption_controller.clips()
              .at(clip_index)
              .toMap()
              .value(QStringLiteral("mediaType"))
              .toString() == QStringLiteral("caption")) {
        reopened_caption_controller.selectClip(clip_index);
        break;
      }
    }
    if (reopened_caption_controller.currentClip().value(QStringLiteral("mediaType")).toString() !=
        QStringLiteral("caption")) {
      return fail("caption variant proof should select a reopened caption cue");
    }
    const QVariantList caption_variant_expectations = {
        QVariantMap{{QStringLiteral("key"), QStringLiteral("landscape")},
                    {QStringLiteral("fontSize"), 50.0},
                    {QStringLiteral("positionY"), 0.86},
                    {QStringLiteral("safeX"), 0.08},
                    {QStringLiteral("safeY"), 0.08}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("vertical")},
                    {QStringLiteral("fontSize"), 60.0},
                    {QStringLiteral("positionY"), 0.72},
                    {QStringLiteral("safeX"), 0.10},
                    {QStringLiteral("safeY"), 0.16}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("square")},
                    {QStringLiteral("fontSize"), 56.0},
                    {QStringLiteral("positionY"), 0.80},
                    {QStringLiteral("safeX"), 0.09},
                    {QStringLiteral("safeY"), 0.10}},
        QVariantMap{{QStringLiteral("key"), QStringLiteral("portrait")},
                    {QStringLiteral("fontSize"), 58.0},
                    {QStringLiteral("positionY"), 0.76},
                    {QStringLiteral("safeX"), 0.09},
                    {QStringLiteral("safeY"), 0.12}},
    };
    for (const auto& expectation_value : caption_variant_expectations) {
      const auto expectation = expectation_value.toMap();
      reopened_caption_controller.applyOutputPreset(
          expectation.value(QStringLiteral("key")).toString());
      if (reopened_caption_controller.activeOutputVariant() !=
          expectation.value(QStringLiteral("key")).toString()) {
        return fail("caption style proof should switch to each supported output aspect");
      }
      const auto caption = reopened_caption_controller.currentClip();
      if (!nearly_equal(
              caption.value(QStringLiteral("titleFontSize")).toDouble(),
              expectation.value(QStringLiteral("fontSize")).toDouble()) ||
          !nearly_equal(
              caption.value(QStringLiteral("titlePositionY")).toDouble(),
              expectation.value(QStringLiteral("positionY")).toDouble()) ||
          !nearly_equal(
              caption.value(QStringLiteral("captionSafeMarginX")).toDouble(),
              expectation.value(QStringLiteral("safeX")).toDouble()) ||
          !nearly_equal(
              caption.value(QStringLiteral("captionSafeMarginY")).toDouble(),
              expectation.value(QStringLiteral("safeY")).toDouble())) {
        std::cerr << "caption aspect mismatch "
                  << expectation.value(QStringLiteral("key")).toString().toStdString()
                  << " size=" << caption.value(QStringLiteral("titleFontSize")).toDouble()
                  << " y=" << caption.value(QStringLiteral("titlePositionY")).toDouble()
                  << " safeX=" << caption.value(QStringLiteral("captionSafeMarginX")).toDouble()
                  << " safeY=" << caption.value(QStringLiteral("captionSafeMarginY")).toDouble()
                  << '\n';
        return fail("each saved output aspect should restore its exact caption size, position, and safe margins");
      }
    }
    const auto caption_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.15"),
            QStringLiteral("-i"), caption_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("crop=240:70:40:160,scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (caption_sample.size() < 3 || static_cast<unsigned char>(caption_sample.at(0)) > 235) {
      return fail("caption text and background should be burned into exported video pixels");
    }

    TimelineDocumentController emphasis_render_controller;
    emphasis_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Caption Emphasis Render")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    emphasis_render_controller.addClipFromGallery(
        QStringLiteral("video:caption-emphasis"), render_video_one,
        QStringLiteral("Caption Emphasis"), 0.0, QString{});
    emphasis_render_controller.generateCaptionsFromTranscript(
        QStringLiteral("ONE TWO THREE."));
    if (!emphasis_render_controller.applyCaptionStyleTemplate(
            QStringLiteral("boxed-explainer"), true) ||
        !emphasis_render_controller.setSelectedCaptionEmphasis(
            QStringLiteral("single-word"), false, true)) {
      return fail("caption emphasis render proof should configure a styled single-word cue");
    }
    const auto decoded_emphasis_frame = [&ffmpeg](const QString& path, double seconds) {
      return process_bytes(
          ffmpeg,
          {
              QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-i"), path,
              QStringLiteral("-ss"), QString::number(seconds, 'f', 3),
              QStringLiteral("-frames:v"), QStringLiteral("1"),
              QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
              QStringLiteral("-f"), QStringLiteral("rawvideo"),
              QStringLiteral("pipe:1"),
          });
    };
    const auto changed_emphasis_bytes = [](const QByteArray& left, const QByteArray& right) {
      if (left.size() != right.size() || left.isEmpty()) {
        return -1;
      }
      auto changed = 0;
      for (qsizetype index = 0; index < left.size(); ++index) {
        const auto delta = qAbs(
            static_cast<int>(static_cast<unsigned char>(left.at(index))) -
            static_cast<int>(static_cast<unsigned char>(right.at(index))));
        if (delta > 8) {
          ++changed;
        }
      }
      return changed;
    };
    const auto single_word_render_path =
        workspace.filePath(QStringLiteral("caption-emphasis-single-word.mp4"));
    if (!emphasis_render_controller.renderCurrentTimeline(single_word_render_path)) {
      return fail("single-word caption emphasis should render through the final export graph");
    }
    const auto single_one = decoded_emphasis_frame(single_word_render_path, 0.10);
    const auto single_two = decoded_emphasis_frame(single_word_render_path, 0.50);
    const auto single_three = decoded_emphasis_frame(single_word_render_path, 0.90);
    if (changed_emphasis_bytes(single_one, single_two) < 250 ||
        changed_emphasis_bytes(single_two, single_three) < 250) {
      return fail("single-word delivery should change the visible caption at the edited word boundaries");
    }

    if (!emphasis_render_controller.setSelectedCaptionEmphasis(
            QStringLiteral("pop"), false, true)) {
      return fail("caption emphasis render proof should enable Pop motion");
    }
    const auto pop_render_path = workspace.filePath(QStringLiteral("caption-emphasis-pop.mp4"));
    if (!emphasis_render_controller.renderCurrentTimeline(pop_render_path)) {
      return fail("Pop caption emphasis should render dynamic scale motion");
    }
    const auto pop_early = decoded_emphasis_frame(pop_render_path, 0.03);
    const auto pop_peak = decoded_emphasis_frame(pop_render_path, 0.20);
    if (changed_emphasis_bytes(pop_early, pop_peak) < 250) {
      return fail("Pop delivery should animate within an active word rather than export a static placeholder");
    }

    if (!emphasis_render_controller.setSelectedCaptionEmphasis(
            QStringLiteral("pop"), true, true)) {
      return fail("caption emphasis render proof should enable reduced-motion delivery");
    }
    const auto reduced_pop_render_path =
        workspace.filePath(QStringLiteral("caption-emphasis-pop-reduced.mp4"));
    if (!emphasis_render_controller.renderCurrentTimeline(reduced_pop_render_path)) {
      return fail("reduced-motion Pop should render through the same final graph");
    }
    const auto reduced_early = decoded_emphasis_frame(reduced_pop_render_path, 0.03);
    const auto reduced_peak = decoded_emphasis_frame(reduced_pop_render_path, 0.20);
    if (changed_emphasis_bytes(reduced_early, reduced_peak) > 80) {
      return fail("reduced-motion delivery should replace Pop motion with one static highlighted word state");
    }

    TimelineDocumentController transcript_edit_controller;
    transcript_edit_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Transcript Rough Cut")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    transcript_edit_controller.addClipFromGallery(
        QStringLiteral("video:transcript-base"),
        render_video_one,
        QStringLiteral("Transcript Base"),
        0.0,
        QString{});
    transcript_edit_controller.generateCaptionsFromTranscript(
        QStringLiteral("One two three. Four five six."));
    const auto initial_transcript = transcript_edit_controller.transcriptSegments();
    if (initial_transcript.size() != 2 ||
        initial_transcript.at(0).toMap().value(QStringLiteral("words")).toList().size() != 3 ||
        initial_transcript.at(1).toMap().value(QStringLiteral("words")).toList().size() != 3 ||
        !nearly_equal(
            initial_transcript.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.6)) {
      return fail("text-based editing should expose ordered transcript segments with absolute word timecodes");
    }
    const auto first_segment_words = initial_transcript.at(0).toMap().value(QStringLiteral("words")).toList();
    const auto second_segment_words = initial_transcript.at(1).toMap().value(QStringLiteral("words")).toList();
    const auto rough_cut_start = first_segment_words.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble();
    const auto rough_cut_end = second_segment_words.at(0).toMap().value(QStringLiteral("endSeconds")).toDouble();
    if (!nearly_equal(rough_cut_start, 0.2) || !nearly_equal(rough_cut_end, 0.8) ||
        !transcript_edit_controller.selectClipsInSequenceRange(rough_cut_start, rough_cut_end) ||
        transcript_edit_controller.selectedClipCount() != 1) {
      return fail("a transcript word range should select the overlapping media without changing the edit");
    }
    transcript_edit_controller.seekSequenceSeconds(1.0);
    transcript_edit_controller.addSequenceMarker(QStringLiteral("After transcript cut"));
    if (!transcript_edit_controller.toggleTrackAutomationKeyframe(0, QStringLiteral("opacity"))) {
      return fail("transcript rough-cut fixture should author downstream track automation");
    }
    const auto duration_before_transcript_cut = transcript_edit_controller.totalDurationSecondsValue();
    const auto extracted_automation = [&transcript_edit_controller]() {
      return transcript_edit_controller.sequenceTracks().at(0).toMap()
          .value(QStringLiteral("automationKeyframes")).toList();
    };
    if (!transcript_edit_controller.extractSequenceRange(rough_cut_start, rough_cut_end) ||
        !nearly_equal(transcript_edit_controller.totalDurationSecondsValue(), duration_before_transcript_cut - 0.6) ||
        transcript_edit_controller.videoTrackCount() != 1 ||
        transcript_edit_controller.audioTrackCount() != 1 ||
        transcript_edit_controller.trackCount() != 3 ||
        transcript_edit_controller.sequenceMarkers().size() != 1 ||
        !nearly_equal(
            transcript_edit_controller.sequenceMarkers().first().toMap().value(QStringLiteral("seconds")).toDouble(),
            0.4) ||
        extracted_automation().size() != 1 ||
        !nearly_equal(extracted_automation().first().toMap().value(QStringLiteral("seconds")).toDouble(), 0.4)) {
      return fail("transcript extraction should remove the exact range and ripple synchronized media, audio, captions, markers, and automation");
    }
    QString extracted_transcript_text;
    for (const auto& segment_value : transcript_edit_controller.transcriptSegments()) {
      extracted_transcript_text += QLatin1Char(' ') +
                                   segment_value.toMap().value(QStringLiteral("text")).toString();
    }
    if (!extracted_transcript_text.simplified().contains(QStringLiteral("One")) ||
        !extracted_transcript_text.simplified().contains(QStringLiteral("five six.")) ||
        extracted_transcript_text.contains(QStringLiteral("two")) ||
        extracted_transcript_text.contains(QStringLiteral("three")) ||
        extracted_transcript_text.contains(QStringLiteral("Four"))) {
      return fail("caption words inside an extracted transcript range should be removed while surrounding words stay timed");
    }
    transcript_edit_controller.undo();
    if (!nearly_equal(transcript_edit_controller.totalDurationSecondsValue(), duration_before_transcript_cut) ||
        transcript_edit_controller.transcriptSegments().size() != 2 ||
        transcript_edit_controller.sequenceMarkers().size() != 1 ||
        !nearly_equal(
            transcript_edit_controller.sequenceMarkers().first().toMap().value(QStringLiteral("seconds")).toDouble(),
            1.0) ||
        extracted_automation().size() != 1 ||
        !nearly_equal(extracted_automation().first().toMap().value(QStringLiteral("seconds")).toDouble(), 1.0)) {
      return fail("a transcript rough cut should undo atomically, including captions and review markers");
    }
    transcript_edit_controller.redo();
    if (!nearly_equal(transcript_edit_controller.totalDurationSecondsValue(), duration_before_transcript_cut - 0.6)) {
      return fail("a transcript rough cut should redo as one deterministic edit");
    }

    TimelineDocumentController text_plan_controller;
    text_plan_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Reviewed Text Edit Plan")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    text_plan_controller.addClipFromGallery(
        QStringLiteral("video:text-plan-red"), render_video_one,
        QStringLiteral("Red Opening"), 0.0, QString{});
    text_plan_controller.addClipFromGallery(
        QStringLiteral("video:text-plan-blue"), render_video_overlay,
        QStringLiteral("Blue Ending"), 0.0, QString{});
    text_plan_controller.generateCaptionsFromTranscript(
        QStringLiteral("Red opening words. Blue ending words."));
    const auto text_plan_segments = text_plan_controller.transcriptSegments();
    if (text_plan_segments.size() != 2) {
      return fail("reviewed text editing needs two stable timed transcript ranges");
    }
    const auto red_segment = text_plan_segments.at(0).toMap();
    const auto blue_segment = text_plan_segments.at(1).toMap();
    const auto red_words = red_segment.value(QStringLiteral("words")).toList();
    if (red_words.size() != 3 ||
        !nearly_equal(red_segment.value(QStringLiteral("startSeconds")).toDouble(), 0.0) ||
        !nearly_equal(blue_segment.value(QStringLiteral("startSeconds")).toDouble(), 1.2, 0.05)) {
      return fail("review plan fixture needs exact sentence and word timing across both shots");
    }
    text_plan_controller.seekSequenceSeconds(1.8);
    text_plan_controller.addSequenceMarker(QStringLiteral("Move with blue sentence"));
    if (!text_plan_controller.toggleTrackAutomationKeyframe(
            0, QStringLiteral("opacity"))) {
      return fail("text edit plan fixture should author downstream automation");
    }
    const auto document_hash_before_plan =
        text_plan_controller.deterministicDocumentHash();
    const auto duration_before_plan =
        text_plan_controller.totalDurationSecondsValue();
    const auto delete_start = red_words.at(1).toMap()
                                  .value(QStringLiteral("startSeconds"))
                                  .toDouble();
    const auto delete_end = red_words.at(1).toMap()
                                .value(QStringLiteral("endSeconds"))
                                .toDouble();
    const auto deletion_id = text_plan_controller.stageTextBasedEditDeletion(
        delete_start, delete_end);
    const auto move_id = text_plan_controller.stageTextBasedEditMove(
        blue_segment.value(QStringLiteral("startSeconds")).toDouble(),
        blue_segment.value(QStringLiteral("endSeconds")).toDouble(),
        -1);
    auto text_plan_preview = text_plan_controller.textBasedEditPreview();
    if (deletion_id.isEmpty() || move_id.isEmpty() ||
        text_plan_controller.pendingTextBasedEdits().size() != 2 ||
        !text_plan_preview.value(QStringLiteral("valid")).toBool() ||
        !text_plan_preview.value(QStringLiteral("nonMutating")).toBool() ||
        text_plan_preview.value(QStringLiteral("operationCount")).toInt() != 2 ||
        text_plan_preview.value(QStringLiteral("deleteCount")).toInt() != 1 ||
        text_plan_preview.value(QStringLiteral("moveCount")).toInt() != 1 ||
        !nearly_equal(
            text_plan_preview.value(QStringLiteral("previewDurationSeconds")).toDouble(),
            duration_before_plan - (delete_end - delete_start),
            0.03) ||
        !text_plan_preview.value(QStringLiteral("previewTranscript"))
             .toString()
             .startsWith(QStringLiteral("Blue ending words.")) ||
        text_plan_controller.deterministicDocumentHash() != document_hash_before_plan ||
        !nearly_equal(
            text_plan_controller.totalDurationSecondsValue(), duration_before_plan) ||
        text_plan_controller.transcriptSegments().first().toMap()
                .value(QStringLiteral("text"))
                .toString() != QStringLiteral("Red opening words.")) {
      return fail("staging delete and rearrange operations should create a truthful review without mutating the sequence");
    }
    if (!text_plan_controller.restorePendingTextBasedEdit(deletion_id) ||
        text_plan_controller.pendingTextBasedEdits().size() != 1 ||
        !nearly_equal(
            text_plan_controller.textBasedEditPreview()
                .value(QStringLiteral("previewDurationSeconds"))
                .toDouble(),
            duration_before_plan,
            0.03)) {
      return fail("Restore should remove one staged deletion without changing media");
    }
    const auto restored_deletion_id =
        text_plan_controller.stageTextBasedEditDeletion(delete_start, delete_end);
    if (restored_deletion_id.isEmpty()) {
      return fail("a restored transcript range should be stageable again");
    }

    text_plan_controller.seekSequenceSeconds(0.1);
    text_plan_controller.addSequenceMarker(QStringLiteral("Invalidate staged plan"));
    if (text_plan_controller.textBasedEditPreview()
            .value(QStringLiteral("valid"))
            .toBool() ||
        text_plan_controller.commitPendingTextBasedEdits()) {
      return fail("a document revision should make a staged text plan visibly stale and non-committable");
    }
    text_plan_controller.undo();
    if (text_plan_controller.deterministicDocumentHash() != document_hash_before_plan ||
        !text_plan_controller.textBasedEditPreview()
             .value(QStringLiteral("valid"))
             .toBool()) {
      return fail("returning to the reviewed document revision should make its plan valid again");
    }
    if (!text_plan_controller.commitPendingTextBasedEdits() ||
        !text_plan_controller.pendingTextBasedEdits().isEmpty() ||
        !nearly_equal(
            text_plan_controller.totalDurationSecondsValue(),
            duration_before_plan - (delete_end - delete_start),
            0.03)) {
      return fail("Commit should apply the complete reviewed text plan atomically");
    }
    QString committed_text;
    for (const auto& segment_value : text_plan_controller.transcriptSegments()) {
      committed_text += QLatin1Char(' ') +
                        segment_value.toMap().value(QStringLiteral("text")).toString();
    }
    const auto committed_automation =
        text_plan_controller.sequenceTracks().at(0).toMap()
            .value(QStringLiteral("automationKeyframes")).toList();
    if (!committed_text.simplified().startsWith(QStringLiteral("Blue ending words.")) ||
        committed_text.contains(QStringLiteral("opening")) ||
        !committed_text.contains(QStringLiteral("Red words.")) ||
        text_plan_controller.sequenceMarkers().size() != 1 ||
        !nearly_equal(
            text_plan_controller.sequenceMarkers().first().toMap()
                .value(QStringLiteral("seconds")).toDouble(),
            0.6,
            0.03) ||
        committed_automation.size() != 1 ||
        !nearly_equal(
            committed_automation.first().toMap()
                .value(QStringLiteral("seconds")).toDouble(),
            0.6,
            0.03)) {
      return fail("committed text edits should reorder shots and ripple captions, markers, and automation together");
    }
    text_plan_controller.undo();
    if (!nearly_equal(
            text_plan_controller.totalDurationSecondsValue(), duration_before_plan) ||
        text_plan_controller.transcriptSegments().first().toMap()
                .value(QStringLiteral("text"))
                .toString() != QStringLiteral("Red opening words.") ||
        !nearly_equal(
            text_plan_controller.sequenceMarkers().first().toMap()
                .value(QStringLiteral("seconds")).toDouble(),
            1.8,
            0.03)) {
      return fail("one undo should restore the complete pre-commit text edit state");
    }
    text_plan_controller.redo();
    if (!text_plan_controller.saveTimeline()) {
      return fail("committed text edit plan should save");
    }
    TimelineDocumentController reopened_text_plan_controller;
    if (!reopened_text_plan_controller.loadTimeline(
            text_plan_controller.savedDocumentPath())) {
      return fail("committed text edit plan should reopen");
    }
    QString reopened_text_plan;
    for (const auto& segment_value : reopened_text_plan_controller.transcriptSegments()) {
      reopened_text_plan += QLatin1Char(' ') +
                            segment_value.toMap().value(QStringLiteral("text")).toString();
    }
    if (!reopened_text_plan.simplified().startsWith(
            QStringLiteral("Blue ending words.")) ||
        !nearly_equal(
            reopened_text_plan_controller.totalDurationSecondsValue(),
            duration_before_plan - (delete_end - delete_start),
            0.03)) {
      return fail("rearranged and deleted transcript edits should survive save and reopen");
    }
    const auto text_plan_render =
        workspace.filePath(QStringLiteral("reviewed-text-plan.mp4"));
    if (!reopened_text_plan_controller.renderCurrentTimeline(text_plan_render)) {
      return fail("the committed text edit plan should render through ordinary delivery");
    }
    const auto opening_text_plan_pixel = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.20"),
            QStringLiteral("-i"), text_plan_render,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("crop=80:80:0:0,scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (opening_text_plan_pixel.size() < 3 ||
        static_cast<unsigned char>(opening_text_plan_pixel.at(2)) < 110 ||
        static_cast<unsigned char>(opening_text_plan_pixel.at(0)) > 100) {
      return fail("encoded delivery should begin with the blue shot moved first by the reviewed transcript plan");
    }

    const auto transcription_fixture_path =
        workspace.filePath(QStringLiteral("timeline-transcription-fixture.json"));
    QFile transcription_fixture_file(transcription_fixture_path);
    const QJsonArray transcription_fixture_words = {
        QJsonObject{
            {QStringLiteral("text"), QStringLiteral("Hello")},
            {QStringLiteral("startSeconds"), 0.10},
            {QStringLiteral("endSeconds"), 0.30},
        },
        QJsonObject{
            {QStringLiteral("text"), QStringLiteral("um")},
            {QStringLiteral("startSeconds"), 0.40},
            {QStringLiteral("endSeconds"), 0.50},
        },
        QJsonObject{
            {QStringLiteral("text"), QStringLiteral("world.")},
            {QStringLiteral("startSeconds"), 0.70},
            {QStringLiteral("endSeconds"), 1.00},
        },
    };
    if (!transcription_fixture_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        transcription_fixture_file.write(
            QJsonDocument(QJsonObject{{QStringLiteral("words"), transcription_fixture_words}})
                .toJson(QJsonDocument::Compact)) <= 0) {
      return fail("automatic transcription proof should write a deterministic timed-word fixture");
    }
    transcription_fixture_file.close();
    const auto previous_transcription_fixture =
        qgetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    qputenv(
        "PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE",
        transcription_fixture_path.toUtf8());

    TimelineDocumentController stale_transcript_controller;
    stale_transcript_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Stale Transcript")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    stale_transcript_controller.addClipFromGallery(
        QStringLiteral("video:stale-transcript"), render_video_one,
        QStringLiteral("Changing Source"), 0.0, QString{});
    if (!wait_for_media_analysis(&stale_transcript_controller)) {
      return fail("stale transcription proof needs authoritative audio metadata");
    }
    stale_transcript_controller.setTranscriptionOptions(QStringLiteral("en-US"), false);
    stale_transcript_controller.generateCaptionsFromTranscript(
        QStringLiteral("Keep this caption."));
    const auto stale_transcript_clips = stale_transcript_controller.clips();
    for (int index = 0; index < stale_transcript_clips.size(); ++index) {
      if (stale_transcript_clips.at(index)
              .toMap()
              .value(QStringLiteral("filePath"))
              .toString() == render_video_one) {
        stale_transcript_controller.selectClip(index);
        break;
      }
    }
    if (stale_transcript_controller.transcribeTimelineAudio() != 1) {
      return fail("stale transcription proof should queue one source revision");
    }
    stale_transcript_controller.setSelectedClipPlaybackRate(1.25);
    if (!wait_for_transcription(&stale_transcript_controller)) {
      return fail("obsolete transcription should settle after its revision is invalidated");
    }
    const auto transcript_stale_state = stale_transcript_controller.staleResultState();
    const auto stale_transcript_segments = stale_transcript_controller.transcriptSegments();
    if (transcript_stale_state.value(QStringLiteral("lastWorkflow")).toString() !=
            QStringLiteral("transcription") ||
        transcript_stale_state.value(QStringLiteral("discardCount")).toInt() < 1 ||
        stale_transcript_segments.size() != 1 ||
        stale_transcript_segments.first()
                .toMap()
                .value(QStringLiteral("text"))
                .toString() != QStringLiteral("Keep this caption.")) {
      return fail("stale transcript results must not replace captions after the source mapping or sequence revision changes");
    }

    TimelineDocumentController automatic_transcript_controller;
    automatic_transcript_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Automatic Transcript")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    automatic_transcript_controller.addClipFromGallery(
        QStringLiteral("video:auto-transcript"),
        render_video_one,
        QStringLiteral("Spoken Source"),
        0.0,
        QString{});
    if (!wait_for_media_analysis(&automatic_transcript_controller)) {
      return fail("automatic transcription needs authoritative audio metadata");
    }
    automatic_transcript_controller.setSelectedClipPlaybackRate(2.0);
    automatic_transcript_controller.generateCaptionsFromTranscript(
        QStringLiteral("Manual captions."));
    if (automatic_transcript_controller.transcribeTimelineAudio() != 1 ||
        !wait_for_transcription(&automatic_transcript_controller)) {
      return fail("automatic transcription should run one unique audio source asynchronously");
    }
    const auto automatic_segments = automatic_transcript_controller.transcriptSegments();
    if (automatic_segments.size() != 1 ||
        automatic_segments.first().toMap().value(QStringLiteral("words")).toList().size() != 3 ||
        automatic_segments.first().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("Hello um world.")) {
      return fail("automatic transcription should atomically replace manual captions with editable timed words");
    }
    const auto automatic_words =
        automatic_segments.first().toMap().value(QStringLiteral("words")).toList();
    if (!nearly_equal(
            automatic_words.at(0).toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.05) ||
        !nearly_equal(
            automatic_words.at(0).toMap().value(QStringLiteral("endSeconds")).toDouble(),
            0.15) ||
        !nearly_equal(
            automatic_words.at(2).toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.35) ||
        !nearly_equal(
            automatic_words.at(2).toMap().value(QStringLiteral("endSeconds")).toDouble(),
            0.50)) {
      return fail("recognized source-word timing should map through trim and playback rate into sequence time");
    }
    automatic_transcript_controller.undo();
    if (automatic_transcript_controller.transcriptSegments().size() != 1 ||
        !automatic_transcript_controller.transcriptSegments()
             .first()
             .toMap()
             .value(QStringLiteral("text"))
             .toString()
             .contains(QStringLiteral("Manual"))) {
      return fail("automatic caption replacement should undo in one step and restore prior captions");
    }
    automatic_transcript_controller.redo();
    if (automatic_transcript_controller.transcriptSegments().size() != 1 ||
        automatic_transcript_controller.transcriptSegments()
                .first()
                .toMap()
                .value(QStringLiteral("text"))
                .toString() != QStringLiteral("Hello um world.")) {
      return fail("automatic caption replacement should redo deterministically");
    }

    TimelineDocumentController cancelled_transcript_controller;
    cancelled_transcript_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Cancelled Transcript")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
    });
    cancelled_transcript_controller.addClipFromGallery(
        QStringLiteral("video:cancelled-transcript"),
        render_video_one,
        QStringLiteral("Cancelled Source"),
        0.0,
        QString{});
    if (!wait_for_media_analysis(&cancelled_transcript_controller) ||
        cancelled_transcript_controller.transcribeTimelineAudio() != 1) {
      return fail("cancellation proof should start a pending transcription");
    }
    cancelled_transcript_controller.cancelTranscription();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (cancelled_transcript_controller.transcriptionBusy() ||
        cancelled_transcript_controller.captionCount() != 0) {
      return fail("cancelling transcription should leave the existing sequence unchanged");
    }
    if (previous_transcription_fixture.isEmpty()) {
      qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    } else {
      qputenv(
          "PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE",
          previous_transcription_fixture);
    }

    TimelineDocumentController beat_controller;
    beat_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Beat Markers")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    beat_controller.addClipFromGallery(
        QStringLiteral("video:beat-analysis"),
        beat_analysis_video,
        QStringLiteral("Four Beats"),
        0.0,
        QString{});
    if (!wait_for_media_analysis(&beat_controller)) {
      return fail("beat analysis fixture should resolve authoritative source audio");
    }
    beat_controller.setSelectedClipPlaybackRate(2.0);
    if (beat_controller.analyzeTimelineBeats() != 1 ||
        !wait_for_beat_analysis(&beat_controller)) {
      return fail("beat analysis should run each unique audio source asynchronously");
    }
    const auto beat_suggestions = beat_controller.beatSuggestions();
    if (beat_suggestions.size() != 4) {
      std::cerr << "beat suggestions=" << beat_suggestions.size()
                << " status=" << beat_controller.beatAnalysisStatus().toStdString() << '\n';
      return fail("the real four-pulse audio fixture should produce four conservative beat suggestions");
    }
    const QVector<double> expected_beat_seconds{0.10, 0.35, 0.60, 0.85};
    for (int beat_index = 0; beat_index < expected_beat_seconds.size(); ++beat_index) {
      if (!nearly_equal(
              beat_suggestions.at(beat_index)
                  .toMap()
                  .value(QStringLiteral("seconds"))
                  .toDouble(),
              expected_beat_seconds.at(beat_index),
              0.04)) {
        return fail("detected source beats should map through clip playback rate into sequence time");
      }
    }
    if (beat_controller.sequenceMarkers().size() != 0 ||
        beat_controller.createBeatMarkersFromAnalysis() != 4 ||
        beat_controller.sequenceMarkers().size() != 4) {
      return fail("beat suggestions should remain inert until one confirmed marker creation edit");
    }
    beat_controller.undo();
    if (!beat_controller.sequenceMarkers().isEmpty()) {
      return fail("creating a detected beat-marker set should undo atomically");
    }
    beat_controller.redo();
    if (beat_controller.sequenceMarkers().size() != 4 ||
        !beat_controller.saveTimeline()) {
      return fail("detected beat markers should redo and persist as ordinary timeline markers");
    }
    TimelineDocumentController reopened_beat_controller;
    if (!reopened_beat_controller.loadTimeline(beat_controller.savedDocumentPath()) ||
        reopened_beat_controller.sequenceMarkers().size() != 4) {
      return fail("detected beat markers should survive save and reopen");
    }

    TimelineDocumentController cancelled_beat_controller;
    cancelled_beat_controller.createTimeline(QStringLiteral("Cancelled Beat Analysis"));
    cancelled_beat_controller.addClipFromGallery(
        QStringLiteral("video:cancelled-beat-analysis"),
        beat_analysis_video,
        QStringLiteral("Cancel Beats"),
        0.0,
        QString{});
    if (!wait_for_media_analysis(&cancelled_beat_controller) ||
        cancelled_beat_controller.analyzeTimelineBeats() != 1) {
      return fail("beat cancellation proof should start an asynchronous analysis");
    }
    cancelled_beat_controller.cancelBeatAnalysis();
    if (cancelled_beat_controller.beatAnalysisBusy() ||
        !cancelled_beat_controller.beatSuggestions().isEmpty() ||
        !cancelled_beat_controller.sequenceMarkers().isEmpty()) {
      return fail("cancelling beat analysis should leave suggestions and sequence markers unchanged");
    }

    TimelineDocumentController cleanup_suggestion_controller;
    cleanup_suggestion_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Reviewed Cleanup")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    cleanup_suggestion_controller.addClipFromGallery(
        QStringLiteral("video:cleanup-base"),
        render_video_one,
        QStringLiteral("Cleanup Base"),
        0.0,
        QString{});
    cleanup_suggestion_controller.generateCaptionsFromTranscript(QStringLiteral("Hello um world."));
    if (!cleanup_suggestion_controller.updateSelectedCaptionWord(0, QStringLiteral("Hello"), 0.0, 0.10) ||
        !cleanup_suggestion_controller.updateSelectedCaptionWord(1, QStringLiteral("um"), 0.75, 0.78)) {
      return fail("cleanup fixture should create a deterministic filler word and long timed speech gap");
    }
    const auto cleanup_suggestions = cleanup_suggestion_controller.transcriptCleanupSuggestions();
    QStringList cleanup_suggestion_ids;
    auto filler_suggestion_count = 0;
    auto speech_gap_suggestion_count = 0;
    for (const auto& suggestion_value : cleanup_suggestions) {
      const auto suggestion = suggestion_value.toMap();
      cleanup_suggestion_ids.push_back(suggestion.value(QStringLiteral("suggestionId")).toString());
      if (suggestion.value(QStringLiteral("kind")).toString() == QStringLiteral("filler")) {
        ++filler_suggestion_count;
      } else if (suggestion.value(QStringLiteral("kind")).toString() == QStringLiteral("speech-gap")) {
        ++speech_gap_suggestion_count;
      }
    }
    if (cleanup_suggestions.size() != 2 || filler_suggestion_count != 1 ||
        speech_gap_suggestion_count != 1 || cleanup_suggestion_ids.contains(QString{})) {
      return fail("cleanup review should surface conservative filler and timed speech-gap suggestions without editing media");
    }
    const auto cleanup_duration_before_apply = cleanup_suggestion_controller.totalDurationSecondsValue();
    const auto cleanup_applied = cleanup_suggestion_controller.applyTranscriptCleanupSuggestions(cleanup_suggestion_ids);
    const auto cleanup_duration_after_apply = cleanup_suggestion_controller.totalDurationSecondsValue();
    const auto cleanup_suggestions_after_apply = cleanup_suggestion_controller.transcriptCleanupSuggestions();
    if (!cleanup_applied ||
        !nearly_equal(
            cleanup_duration_after_apply,
            cleanup_duration_before_apply - 0.56) ||
        !cleanup_suggestions_after_apply.isEmpty()) {
      std::cerr << "cleanup applied=" << cleanup_applied
                << " before=" << cleanup_duration_before_apply
                << " after=" << cleanup_duration_after_apply
                << " remaining=" << cleanup_suggestions_after_apply.size()
                << " status=" << cleanup_suggestion_controller.status().toStdString() << '\n';
      return fail("reviewed cleanup suggestions should batch-apply exact non-overlapping ranges in one ripple edit");
    }
    QString cleaned_transcript;
    for (const auto& segment_value : cleanup_suggestion_controller.transcriptSegments()) {
      cleaned_transcript += QLatin1Char(' ') +
                            segment_value.toMap().value(QStringLiteral("text")).toString();
    }
    if (!cleaned_transcript.contains(QStringLiteral("Hello")) ||
        !cleaned_transcript.contains(QStringLiteral("world.")) ||
        cleaned_transcript.contains(QStringLiteral("um"))) {
      return fail("applying filler cleanup should preserve surrounding transcript words and remove only the reviewed filler");
    }
    cleanup_suggestion_controller.undo();
    if (!nearly_equal(cleanup_suggestion_controller.totalDurationSecondsValue(), cleanup_duration_before_apply) ||
        cleanup_suggestion_controller.transcriptCleanupSuggestions().size() != 2) {
      return fail("a reviewed cleanup batch should undo in one step and restore every suggestion range");
    }
    cleanup_suggestion_controller.redo();
    if (!nearly_equal(
            cleanup_suggestion_controller.totalDurationSecondsValue(),
            cleanup_duration_before_apply - 0.56)) {
      return fail("a reviewed cleanup batch should redo as one deterministic edit");
    }

    TimelineDocumentController audio_silence_controller;
    audio_silence_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Audio Silence Review")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    audio_silence_controller.addClipFromGallery(
        QStringLiteral("video:audio-silence"),
        silence_analysis_video,
        QStringLiteral("Known Silence"),
        0.0,
        QString{});
    if (!wait_for_media_analysis(&audio_silence_controller) ||
        !audio_silence_controller.currentClip().value(QStringLiteral("hasAudio")).toBool()) {
      return fail("audio silence proof needs analyzed source metadata with an audio stream");
    }
    if (audio_silence_controller.analyzeTimelineSilence() != 1 ||
        !wait_for_silence_analysis(&audio_silence_controller)) {
      return fail("audio silence analysis should queue and finish one online audio clip");
    }
    const auto analyzed_silence_clip = audio_silence_controller.currentClip();
    const auto detected_silence_ranges =
        analyzed_silence_clip.value(QStringLiteral("audioSilenceRanges")).toList();
    if (analyzed_silence_clip.value(QStringLiteral("silenceAnalysisStatus")).toString() !=
            QStringLiteral("ready") ||
        detected_silence_ranges.size() != 1) {
      std::cerr << "silence status="
                << analyzed_silence_clip.value(QStringLiteral("silenceAnalysisStatus"))
                       .toString()
                       .toStdString()
                << " ranges=" << detected_silence_ranges.size()
                << " analysis=" << audio_silence_controller.silenceAnalysisStatus().toStdString()
                << '\n';
      return fail("FFmpeg silence analysis should store the known source-time silent interval");
    }
    const auto detected_silence = detected_silence_ranges.first().toMap();
    if (!nearly_equal(
            detected_silence.value(QStringLiteral("sourceStartSeconds")).toDouble(),
            0.45,
            0.04) ||
        !nearly_equal(
            detected_silence.value(QStringLiteral("sourceEndSeconds")).toDouble(),
            1.25,
            0.04)) {
      return fail("stored silence ranges should retain source time instead of baking sequence position");
    }
    const auto audio_silence_suggestions = audio_silence_controller.transcriptCleanupSuggestions();
    if (audio_silence_suggestions.size() != 1 ||
        audio_silence_suggestions.first().toMap().value(QStringLiteral("kind")).toString() !=
            QStringLiteral("audio-silence") ||
        !nearly_equal(
            audio_silence_suggestions.first().toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.54,
            0.04) ||
        !nearly_equal(
            audio_silence_suggestions.first().toMap().value(QStringLiteral("endSeconds")).toDouble(),
            1.16,
            0.04)) {
      return fail("detected source silence should become one conservative sequence-time cleanup suggestion");
    }
    if (!audio_silence_controller.saveTimeline()) {
      return fail("audio silence analysis should persist with the timeline document");
    }
    TimelineDocumentController reopened_audio_silence_controller;
    if (!reopened_audio_silence_controller.loadTimeline(
            audio_silence_controller.savedDocumentPath()) ||
        reopened_audio_silence_controller.currentClip()
                .value(QStringLiteral("audioSilenceRanges"))
                .toList()
                .size() != 1 ||
        reopened_audio_silence_controller.transcriptCleanupSuggestions().size() != 1) {
      return fail("audio-derived cleanup suggestions should survive save and reopen");
    }
    reopened_audio_silence_controller.setSelectedClipPlaybackRate(1.25);
    const auto rate_mapped_suggestions =
        reopened_audio_silence_controller.transcriptCleanupSuggestions();
    if (rate_mapped_suggestions.size() != 1 ||
        !nearly_equal(
            rate_mapped_suggestions.first().toMap().value(QStringLiteral("startSeconds")).toDouble(),
            0.45,
            0.03) ||
        !nearly_equal(
            rate_mapped_suggestions.first().toMap().value(QStringLiteral("endSeconds")).toDouble(),
            0.91,
            0.03)) {
      return fail("source-time silence should remap through playback rate into current sequence time");
    }
    const auto silence_duration_before_apply =
        reopened_audio_silence_controller.totalDurationSecondsValue();
    const auto silence_suggestion = rate_mapped_suggestions.first().toMap();
    const auto silence_remove_duration =
        silence_suggestion.value(QStringLiteral("durationSeconds")).toDouble();
    if (!reopened_audio_silence_controller.applyTranscriptCleanupSuggestions(
            {silence_suggestion.value(QStringLiteral("suggestionId")).toString()}) ||
        !nearly_equal(
            reopened_audio_silence_controller.totalDurationSecondsValue(),
            silence_duration_before_apply - silence_remove_duration,
            0.01)) {
      return fail("a reviewed waveform silence suggestion should apply its exact mapped ripple range");
    }
    reopened_audio_silence_controller.undo();
    if (!nearly_equal(
            reopened_audio_silence_controller.totalDurationSecondsValue(),
            silence_duration_before_apply,
            0.01) ||
        reopened_audio_silence_controller.transcriptCleanupSuggestions().size() != 1) {
      return fail("waveform-derived cleanup should undo atomically and restore its reviewed suggestion");
    }
    reopened_audio_silence_controller.redo();
    if (!nearly_equal(
            reopened_audio_silence_controller.totalDurationSecondsValue(),
            silence_duration_before_apply - silence_remove_duration,
            0.01)) {
      return fail("waveform-derived cleanup should redo as one deterministic edit");
    }

    TimelineDocumentController keyframe_render_controller;
    keyframe_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Keyframed Render")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    keyframe_render_controller.addClipFromGallery(
        QStringLiteral("video:keyframe-render"),
        render_video_two,
        QStringLiteral("Animated Blue"),
        0.0,
        QString{});
    keyframe_render_controller.seekSequenceSeconds(0.0);
    keyframe_render_controller.toggleSelectedClipKeyframe(QStringLiteral("opacity"));
    keyframe_render_controller.setSelectedClipOpacity(0.10);
    keyframe_render_controller.toggleTrackAutomationKeyframe(0, QStringLiteral("opacity"));
    keyframe_render_controller.setTrackAutomationValue(0, QStringLiteral("opacity"), 0.30);
    keyframe_render_controller.seekSequenceSeconds(1.0);
    keyframe_render_controller.setSelectedClipOpacity(1.0);
    keyframe_render_controller.setTrackAutomationValue(0, QStringLiteral("opacity"), 1.0);
    if (!keyframe_render_controller.setSelectedClipKeyframeBezierHandle(
            QStringLiteral("opacity"), 0.0, QStringLiteral("out"), 0.20, 0.90)) {
      return fail("rendered keyframe fixture should author a custom weighted Bezier curve");
    }
    keyframe_render_controller.seekSequenceSeconds(0.50);
    if (keyframe_render_controller.currentClip()
            .value(QStringLiteral("opacity")).toDouble() <= 0.80) {
      return fail("Program preview should evaluate the custom Bezier curve before export");
    }
    if (!keyframe_render_controller.saveTimeline()) {
      return fail("saving a keyframed render should succeed");
    }
    const auto keyframe_render_job_id = QStringLiteral("timeline-render-%1").arg(
        keyframe_render_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(keyframe_render_job_id)) {
      return fail("time-varying keyframe expressions should render through ffmpeg");
    }
    const auto early_keyframe_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.10"),
            QStringLiteral("-i"), keyframe_render_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    const auto late_keyframe_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.95"),
            QStringLiteral("-i"), keyframe_render_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    const auto middle_keyframe_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.50"),
            QStringLiteral("-i"), keyframe_render_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (early_keyframe_sample.size() < 3 || middle_keyframe_sample.size() < 3 ||
        late_keyframe_sample.size() < 3 ||
        static_cast<unsigned char>(late_keyframe_sample.at(2)) <=
            static_cast<unsigned char>(early_keyframe_sample.at(2)) + 80 ||
        static_cast<unsigned char>(middle_keyframe_sample.at(2)) <= 120) {
      return fail("exported opacity keyframes should preserve the weighted Bezier curve over sequence time");
    }

    TimelineDocumentController roving_render_controller;
    roving_render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Roving Keyframe Render")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    roving_render_controller.addClipFromGallery(
        QStringLiteral("video:roving-keyframe-render"), render_video_two,
        QStringLiteral("Roving Blue"), 0.0, QString{});
    roving_render_controller.seekSequenceSeconds(0.0);
    roving_render_controller.toggleSelectedClipKeyframe(QStringLiteral("opacity"));
    roving_render_controller.setSelectedClipOpacity(0.10);
    roving_render_controller.seekSequenceSeconds(0.20);
    roving_render_controller.setSelectedClipOpacity(0.90);
    roving_render_controller.seekSequenceSeconds(1.0);
    roving_render_controller.setSelectedClipOpacity(0.20);
    if (roving_render_controller.setSelectedClipKeyframesRoving(
            QStringLiteral("opacity"), QVariantList{0.20}, true) != 1) {
      return fail("the encoded fixture should author one roving opacity keyframe");
    }
    const auto roving_render_points = roving_render_controller.currentClip()
                                          .value(QStringLiteral("keyframes")).toList();
    const auto roving_peak_seconds = roving_render_points.at(1).toMap()
                                         .value(QStringLiteral("seconds")).toDouble();
    roving_render_controller.seekSequenceSeconds(roving_peak_seconds);
    if (roving_render_controller.currentClip()
            .value(QStringLiteral("opacity")).toDouble() < 0.88) {
      return fail("Program should evaluate the roving keyframe at its computed time");
    }
    const auto roving_render_path =
        workspace.filePath(QStringLiteral("roving-keyframe-render.mp4"));
    if (!roving_render_controller.renderCurrentTimeline(roving_render_path)) {
      return fail("roving keyframes should render through the ordinary delivery path");
    }
    const auto roving_early_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.05"),
            QStringLiteral("-i"), roving_render_path,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    const auto roving_peak_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"),
            QString::number(roving_peak_seconds, 'f', 4),
            QStringLiteral("-i"), roving_render_path,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (roving_early_sample.size() < 3 || roving_peak_sample.size() < 3 ||
        static_cast<unsigned char>(roving_peak_sample.at(2)) <=
            static_cast<unsigned char>(roving_early_sample.at(2)) + 100) {
      return fail("Program and encoded export should share the computed roving-key timing");
    }

    TimelineDocumentController render_controller;
    render_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Rendered Timeline")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
    });
    render_controller.addClipFromGallery(QStringLiteral("video:red"), render_video_one, QStringLiteral("Red Clip"), 0.0, QString{});
    render_controller.addClipFromGallery(QStringLiteral("video:blue"), render_video_two, QStringLiteral("Blue Clip"), 0.0, QString{});
    render_controller.selectClip(0);
    render_controller.setSelectedTrimStartSeconds(0.10);
    render_controller.setSelectedTrimEndSeconds(0.70);
    render_controller.setSelectedClipOpacity(0.7);
    render_controller.setSelectedClipAudioGain(-3.0);
    render_controller.selectClip(1);
    render_controller.setSelectedTrimStartSeconds(0.20);
    render_controller.setSelectedTrimEndSeconds(0.80);
    render_controller.setSelectedFrameRotationDegrees(12.0);
    render_controller.setSelectedClipPlaybackRate(1.1);
    const auto render_marker_id = render_controller.addSequenceMarker(QStringLiteral("Cut"));
    render_controller.setSequenceMarkerNote(render_marker_id, QStringLiteral("Render-ready"));
    render_controller.setSequenceMarkerStatus(render_marker_id, QStringLiteral("exported"));
    render_controller.setSequenceMarkerCompleted(render_marker_id, true);

    if (!render_controller.saveTimeline()) {
      return fail("saving a rendered timeline should succeed when ffmpeg is available");
    }

    const auto render_job_id =
        QStringLiteral("timeline-render-%1").arg(render_controller.timelineAssetId().section(':', 1));
    if (!wait_for_persisted_job(render_job_id)) {
      return fail("queued timeline renders should complete through the shared job runner");
    }

    if (!QFileInfo::exists(render_controller.savedDocumentPath()) ||
        !QFileInfo::exists(render_controller.savedRenderPath())) {
      return fail("saving a timeline should persist both the document and rendered video");
    }

    const auto opacity_sample = process_bytes(
        ffmpeg,
        {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-ss"), QStringLiteral("0.20"),
            QStringLiteral("-i"), render_controller.savedRenderPath(),
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            QStringLiteral("-vf"), QStringLiteral("scale=1:1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("pipe:1"),
        });
    if (opacity_sample.size() < 3) {
      return fail("render verification should be able to sample the opacity-adjusted output frame");
    }
    const auto red = static_cast<unsigned char>(opacity_sample.at(0));
    const auto green = static_cast<unsigned char>(opacity_sample.at(1));
    const auto blue = static_cast<unsigned char>(opacity_sample.at(2));
    if (red < 130 || red > 220 || green > 45 || blue > 45) {
      return fail("timeline export should apply clip opacity against the black program background");
    }

    QString probe_error;
    const auto saved_render_duration = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"),
            QStringLiteral("error"),
            QStringLiteral("-show_entries"),
            QStringLiteral("format=duration"),
            QStringLiteral("-of"),
            QStringLiteral("default=nw=1:nk=1"),
            render_controller.savedRenderPath(),
        },
        &probe_error);
    if (saved_render_duration.isEmpty()) {
      std::cerr << "ffprobe saved render duration failed: " << probe_error.toStdString() << '\n';
      return fail("saved timeline renders should be probeable as playable media");
    }

    bool duration_ok = false;
    const auto parsed_duration = saved_render_duration.toDouble(&duration_ok);
    if (!duration_ok || parsed_duration <= 0.0) {
      return fail("saved timeline renders should report a nonzero duration");
    }

    const auto saved_render_streams = process_stdout(
        ffprobe,
        {
            QStringLiteral("-v"),
            QStringLiteral("error"),
            QStringLiteral("-select_streams"),
            QStringLiteral("v:0"),
            QStringLiteral("-show_entries"),
            QStringLiteral("stream=codec_type"),
            QStringLiteral("-of"),
            QStringLiteral("csv=p=0"),
            render_controller.savedRenderPath(),
        },
        &probe_error);
    if (saved_render_streams.isEmpty()) {
      std::cerr << "ffprobe saved render streams failed: " << probe_error.toStdString() << '\n';
      return fail("saved timeline renders should contain a readable video stream");
    }

    QFile saved_document(render_controller.savedDocumentPath());
    if (!saved_document.open(QIODevice::ReadOnly)) {
      return fail("saved timeline documents should be readable");
    }
    const auto saved_json = QJsonDocument::fromJson(saved_document.readAll()).object();
    if (saved_json.value(QStringLiteral("tracks")).toArray().size() != 2) {
      return fail("saved timelines should persist their track-based sequence layout");
    }
    const auto saved_format = saved_json.value(QStringLiteral("sequenceFormat")).toObject();
    if (!saved_format.contains(QStringLiteral("audioSampleRate")) ||
        !saved_json.contains(QStringLiteral("projectBinding")) ||
        !saved_json.contains(QStringLiteral("exportContainer")) ||
        !saved_json.contains(QStringLiteral("exportCodec")) ||
        saved_json.value(QStringLiteral("exportPreset")).toString() != QStringLiteral("instagram")) {
      return fail("saved timelines should persist the expanded sequence and export settings");
    }

    const auto records = loadTimelineAssetRecords();
    if (records.isEmpty()) {
      return fail("saving a timeline should update the gallery timeline index");
    }

    bool found_saved_timeline = false;
    for (const auto& record : records) {
      if (record.document_path == render_controller.savedDocumentPath() &&
          record.render_path == render_controller.savedRenderPath()) {
        found_saved_timeline = true;
        break;
      }
    }
    if (!found_saved_timeline) {
      return fail("timeline index should contain the newly saved timeline asset");
    }

    const auto persisted_jobs = loadPersistedJobRecords();
    bool found_timeline_render_job = false;
    for (const auto& job : persisted_jobs) {
      if (job.job_kind == QStringLiteral("timeline_render") &&
          job.asset_id == render_controller.timelineAssetId() &&
          job.status == QStringLiteral("Completed") &&
          job.output_path == render_controller.savedRenderPath()) {
        found_timeline_render_job = true;
        break;
      }
    }
    if (!found_timeline_render_job) {
      return fail("saving a timeline should persist a completed shared render job record");
    }

    TimelineDocumentController load_controller;
    if (!load_controller.loadTimeline(render_controller.savedDocumentPath())) {
      return fail("saved timeline documents should load successfully");
    }

    if (load_controller.clipCount() != 2 ||
        load_controller.timelineName() != QStringLiteral("Rendered Timeline")) {
      return fail("loading a saved timeline should restore its clips and metadata");
    }
    if (load_controller.sequenceMarkers().size() != 1) {
      return fail("saved timelines should restore sequence markers");
    }

    const auto loaded_clips = load_controller.clips();
    if (loaded_clips.at(0).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.10 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.70 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("opacity")).toDouble() != 0.7 ||
        loaded_clips.at(0).toMap().value(QStringLiteral("audioGainDb")).toDouble() != -3.0 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimStartSeconds")).toDouble() != 0.20 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("trimEndSeconds")).toDouble() != 0.80 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("frameRotationDegrees")).toDouble() != 12.0 ||
        loaded_clips.at(1).toMap().value(QStringLiteral("playbackRate")).toDouble() != 1.1 ||
        load_controller.sequenceMarkers().size() != 1) {
      return fail("loading a saved timeline should preserve extended clip and marker state");
    }
    const auto loaded_marker = load_controller.sequenceMarkers().at(0).toMap();
    if (loaded_marker.value(QStringLiteral("note")).toString() != QStringLiteral("Render-ready") ||
        loaded_marker.value(QStringLiteral("status")).toString() != QStringLiteral("exported") ||
        !loaded_marker.value(QStringLiteral("completed")).toBool()) {
      return fail("loading a saved timeline should preserve marker review metadata");
    }

    load_controller.selectClip(0);
    load_controller.addSelectedClipEffect(QStringLiteral("Transform"));
    load_controller.assignSelectedClipTransition(QStringLiteral("out"), QStringLiteral("Fade Out"), 0.25);
    if (load_controller.currentClip().value(QStringLiteral("effectStack")).toList().size() != 1 ||
        load_controller.currentClip().value(QStringLiteral("transitionOutName")).toString() != QStringLiteral("Fade Out")) {
      return fail("loaded timelines should accept new effects and transitions on selected clips");
    }

    if (!QFile::remove(render_controller.savedDocumentPath())) {
      return fail("saved timeline document should be removable for recovery testing");
    }

    TimelineDocumentController recovery_controller;
    if (!recovery_controller.loadTimeline(render_controller.savedDocumentPath())) {
      return fail("timeline recovery should load from the persisted backup when the document file is missing");
    }
    if (recovery_controller.timelineName() != QStringLiteral("Rendered Timeline") ||
        recovery_controller.clipCount() != 2 ||
        recovery_controller.sequenceMarkers().size() != 1) {
      return fail("timeline recovery should restore timeline state from the persisted backup");
    }

    TimelineDocumentController short_form_controller;
    short_form_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Short Form Release Proof")},
        {QStringLiteral("width"), 320},
        {QStringLiteral("height"), 240},
        {QStringLiteral("frameRate"), 30.0},
        {QStringLiteral("exportPreset"), QStringLiteral("TikTok")},
    });
    short_form_controller.addClipFromGallery(
        QStringLiteral("video:short-form"), render_video_one,
        QStringLiteral("Short Form Source"), 0.0, QString{});
    short_form_controller.setSelectedFrameMode(QStringLiteral("fill"));
    short_form_controller.addSelectedClipEffect(QStringLiteral("Exposure"));
    short_form_controller.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.15);
    short_form_controller.setSelectedClipAudioGain(-3.0);
    if (!short_form_controller.setTrackAudioSetting(
            1, QStringLiteral("audioBus"), QStringLiteral("dialogue"))) {
      return fail("the representative short-form workflow should route its imported dialogue mix");
    }
    short_form_controller.generateCaptionsFromTranscript(
        QStringLiteral("Ready for the final social cut."));
    short_form_controller.seekSequenceSeconds(0.5);
    short_form_controller.addReviewNote(
        QStringLiteral("Approved cut"), QStringLiteral("Caption and mix checked."),
        QStringLiteral("Producer"), QStringLiteral("approved"));
    const auto short_form_path = workspace.filePath(QStringLiteral("short-form-release-proof.mp4"));
    if (short_form_controller.captionCount() != 1 ||
        short_form_controller.reviewNotes().size() != 1 ||
        !short_form_controller.renderCurrentTimeline(short_form_path) ||
        QFileInfo(short_form_path).size() < 1024) {
      return fail("one representative project should complete import, edit, caption, review, mix, and export without placeholders");
    }
    QProcess short_form_probe;
    short_form_probe.start(
        ffprobe,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type"),
         QStringLiteral("-of"), QStringLiteral("csv=p=0"), short_form_path});
    if (!short_form_probe.waitForFinished(30000)) {
      return fail("the representative short-form export should be probeable");
    }
    const auto short_form_streams = QString::fromUtf8(short_form_probe.readAllStandardOutput());
    if (!short_form_streams.contains(QStringLiteral("video")) ||
        !short_form_streams.contains(QStringLiteral("audio"))) {
      return fail("the representative short-form export should contain playable picture and mixed audio");
    }

    const auto integrity_original_dir = workspace.filePath(QStringLiteral("integrity-original"));
    const auto integrity_relink_dir = workspace.filePath(QStringLiteral("integrity-relink"));
    QDir().mkpath(integrity_original_dir);
    QDir().mkpath(integrity_relink_dir);
    const auto integrity_source =
        QDir(integrity_original_dir).filePath(QStringLiteral("integrity-source.png"));
    const auto integrity_duplicate =
        workspace.filePath(QStringLiteral("integrity-duplicate.png"));
    QByteArray integrity_bytes(256 * 1024, 'a');
    for (auto index = 0; index < integrity_bytes.size(); index += 997) {
      integrity_bytes[index] = static_cast<char>((index / 997) % 251);
    }
    QFile integrity_source_file(integrity_source);
    if (!integrity_source_file.open(QIODevice::WriteOnly) ||
        integrity_source_file.write(integrity_bytes) != integrity_bytes.size()) {
      return fail("the source-integrity fixture should write a large deterministic source");
    }
    integrity_source_file.close();
    if (!QFile::copy(integrity_source, integrity_duplicate)) {
      return fail("the consolidation-integrity fixture should copy matching source bytes");
    }

    TimelineDocumentController integrity_controller;
    integrity_controller.createTimeline(QStringLiteral("Media Integrity Proof"));
    integrity_controller.addClipFromGallery(
        QStringLiteral("asset:canonical"), integrity_source,
        QStringLiteral("Canonical Integrity Source"), 4.0, QString{});
    integrity_controller.addClipFromGallery(
        QStringLiteral("asset:duplicate"), integrity_duplicate,
        QStringLiteral("Duplicate Integrity Source"), 4.0, QString{});
    if (!wait_for_source_integrity(&integrity_controller) ||
        integrity_controller.currentClip()
                .value(QStringLiteral("sourceContentSha256")).toString().size() != 64) {
      return fail("ingest should record a non-blocking full-file SHA-256 for every source");
    }
    const QVariantMap integrity_canonical_asset{
        {QStringLiteral("assetId"), QStringLiteral("asset:canonical")},
        {QStringLiteral("duplicateGroupKey"), QStringLiteral("integrity-group")},
        {QStringLiteral("filePath"), integrity_source},
        {QStringLiteral("durationSeconds"), 4.0},
    };
    const QVariantList integrity_duplicate_assets{
        QVariantMap{
            {QStringLiteral("assetId"), QStringLiteral("asset:duplicate")},
            {QStringLiteral("duplicateGroupKey"), QStringLiteral("integrity-group")},
        },
    };
    if (integrity_controller.consolidateDuplicateMedia(
            integrity_canonical_asset, integrity_duplicate_assets) != 1 ||
        !wait_for_source_integrity(&integrity_controller)) {
      return fail("duplicate consolidation should re-verify the canonical source checksum in the background");
    }
    const auto trusted_source_sha256 = integrity_controller.currentClip()
                                           .value(QStringLiteral("sourceContentSha256"))
                                           .toString();
    if (trusted_source_sha256.size() != 64 || !integrity_controller.saveTimeline()) {
      return fail("the trusted source checksum should persist with the saved timeline");
    }
    QFile saved_integrity_document(integrity_controller.savedDocumentPath());
    if (!saved_integrity_document.open(QIODevice::ReadOnly) ||
        !saved_integrity_document.readAll().contains(trusted_source_sha256.toUtf8())) {
      return fail("the saved timeline should contain the trusted full-file source checksum");
    }

    const auto relinked_integrity_source =
        QDir(integrity_relink_dir).filePath(QStringLiteral("integrity-source.png"));
    if (!QFile::rename(integrity_source, relinked_integrity_source)) {
      return fail("the integrity fixture should become offline before relink");
    }
    const auto relink_preview =
        integrity_controller.previewBatchRelink({integrity_relink_dir});
    auto relink_substitutions = relink_preview.value(QStringLiteral("substitutions")).toList();
    if (relink_substitutions.size() != 1) {
      return fail("batch relink should find the offline checksum-tracked source");
    }
    auto approved_relink = relink_substitutions.first().toMap();
    if (!approved_relink.value(QStringLiteral("recommended")).toBool() ||
        approved_relink.value(QStringLiteral("candidatePath")).toString() !=
            relinked_integrity_source) {
      return fail("full SHA-256 evidence should recommend only the exact relink candidate");
    }
    approved_relink.insert(QStringLiteral("approved"), true);
    if (integrity_controller.applyBatchRelink({approved_relink}) != 1 ||
        !wait_for_source_integrity(&integrity_controller) ||
        integrity_controller.currentClip()
                .value(QStringLiteral("sourceContentSha256")).toString() !=
            trusted_source_sha256) {
      return fail("approved relink should preserve and re-verify the trusted source checksum");
    }

    QFile tampered_source(relinked_integrity_source);
    if (!tampered_source.open(QIODevice::ReadWrite) ||
        !tampered_source.seek(128 * 1024)) {
      return fail("the integrity fixture should open for middle-of-file tampering");
    }
    const auto original_middle_byte = tampered_source.read(1);
    if (original_middle_byte.size() != 1 || !tampered_source.seek(128 * 1024) ||
        tampered_source.write(
            QByteArray(1, static_cast<char>(original_middle_byte.at(0) ^ 0x5a))) != 1 ||
        !tampered_source.setFileTime(
            QDateTime::currentDateTimeUtc().addSecs(2),
            QFileDevice::FileModificationTime)) {
      return fail("the source-integrity fixture should change only a middle byte and revision time");
    }
    tampered_source.close();
    integrity_controller.loadSourceClipFromGallery(
        QStringLiteral("asset:canonical"), relinked_integrity_source,
        QStringLiteral("Tampered Integrity Source"), 4.0, QString{});
    if (!wait_for_source_integrity(
            &integrity_controller, QStringLiteral("mismatch")) ||
        integrity_controller.currentClip()
                .value(QStringLiteral("sourceContentSha256")).toString() !=
            trusted_source_sha256) {
      return fail("a middle-byte tamper must fail full SHA-256 without replacing the trusted ingest checksum");
    }
    const auto tampered_health = integrity_controller.timelineProjectHealth();
    if (tampered_health.value(QStringLiteral("integritySchema")).toString() !=
            QStringLiteral("timeline-media-integrity:v1") ||
        tampered_health.value(QStringLiteral("sourceIdentityAlgorithm")).toString() !=
            QStringLiteral("sha256-full-v1") ||
        tampered_health.value(QStringLiteral("integrityMismatchCount")).toInt() < 1 ||
        tampered_health.value(QStringLiteral("blockingCount")).toInt() < 1) {
      return fail("Project Health should block delivery after a full-source checksum mismatch");
    }
  }

  return EXIT_SUCCESS;
}
