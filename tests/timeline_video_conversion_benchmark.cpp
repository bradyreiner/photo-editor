#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QVideoFrameFormat>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace photo_editor::desktop {
struct TimelinePlaybackFrameTestAccess {
  static void submit(TimelinePlaybackController& controller, const QVideoFrame& frame, bool camera) {
    controller.active_video_input_transform_ = camera ? QStringLiteral("slog3-sgamut3-cine") : QString{};
    controller.native_media_enabled_ = true;
    controller.handleVideoFrame(frame);
    controller.native_media_enabled_ = false;
  }
  static bool active(const TimelinePlaybackController& controller) { return controller.frame_processing_active_; }
  static QImage image(const TimelinePlaybackController& controller) { return controller.frame_image_state_->requestImage(nullptr, {}); }
};
}
using namespace photo_editor::desktop;
namespace {
QVideoFrame fixture(QVideoFrameFormat::PixelFormat format, QSize size, bool full, QVideoFrameFormat::ColorSpace matrix, bool rotate) {
  QVideoFrameFormat description(size, format);
  description.setColorRange(full ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);
  description.setColorSpace(matrix);
  description.setColorTransfer(QVideoFrameFormat::ColorTransfer_BT709);
  QVideoFrame frame(description);
  if (!frame.map(QVideoFrame::WriteOnly)) return {};
  const bool ten = format == QVideoFrameFormat::Format_P010 || format == QVideoFrameFormat::Format_YUV420P10;
  const bool sixteen = format == QVideoFrameFormat::Format_P016;
  for (int plane = 0; plane < frame.planeCount(); ++plane) {
    auto* bytes = frame.bits(plane);
    const auto count = frame.mappedBytes(plane);
    if (ten || sixteen) {
      for (int i = 0; i + 1 < count; i += 2) {
        auto value = static_cast<std::uint16_t>((i / 2 * 37 + plane * 139) & (ten ? 1023 : 65535));
        if (format == QVideoFrameFormat::Format_P010) value = static_cast<std::uint16_t>(value << 6);
        std::memcpy(bytes + i, &value, 2);
      }
    } else {
      for (int i = 0; i < count; ++i) bytes[i] = static_cast<uchar>((i * 37 + plane * 139) & 255);
    }
  }
  frame.unmap();
  if (rotate) { frame.setRotation(QtVideo::Rotation::Clockwise90); frame.setMirrored(true); }
  return frame;
}
bool wait_frame(TimelinePlaybackController& controller) {
  QElapsedTimer timer;
  timer.start();
  while (TimelinePlaybackFrameTestAccess::active(controller) && timer.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    QThread::usleep(100);
  }
  return !TimelinePlaybackFrameTestAccess::active(controller);
}
QString digest(const QImage& image) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (int row = 0; row < image.height(); ++row)
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(row)), image.width() * image.depth() / 8));
  return QString::fromLatin1(hash.result().toHex());
}
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS", "1");
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temporary.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/conversion_fixture");
  TimelineDocumentController document;
  TimelinePlaybackController controller(&document);
  QQuickItem output;
  controller.attachVideoOutput(&output);
  QJsonArray records;
  const auto run = [&](QString name, QVideoFrameFormat::PixelFormat format, QSize size, QSize target,
                       bool full, QVideoFrameFormat::ColorSpace matrix, bool camera, bool rotate, int iterations) {
    const auto frame = fixture(format, size, full, matrix, rotate);
    if (!frame.isValid()) return false;
    output.setSize(target);
    QJsonArray times;
    QString expected_hash;
    QImage last;
    for (int iteration = -1; iteration < iterations; ++iteration) {
      TimelinePlaybackFrameTestAccess::submit(controller, frame, camera);
      if (!wait_frame(controller)) return false;
      last = TimelinePlaybackFrameTestAccess::image(controller);
      if (last.width() <= 1 || !last.text("timelineVideoColorDecodeStatus").contains("normalized")) return false;
      const auto actual_hash = digest(last);
      if (iteration == -1) expected_hash = actual_hash;
      if (actual_hash != expected_hash) return false;
      if (iteration >= 0) times.append(last.text("timelineVideoMetricDecodeNormalizeMs").toDouble());
    }
    records.append(QJsonObject{{"name", name}, {"decode_ms", times}, {"sha256", expected_hash},
        {"width", last.width()}, {"height", last.height()}, {"format", int(last.format())},
        {"color_space", QString::fromLatin1(QCryptographicHash::hash(last.colorSpace().iccProfile(), QCryptographicHash::Sha256).toHex())}});
    return true;
  };
  using F = QVideoFrameFormat;
  for (const auto format : {F::Format_YUV420P, F::Format_YV12, F::Format_YUV422P, F::Format_NV12,
      F::Format_NV21, F::Format_UYVY, F::Format_YUYV, F::Format_P010, F::Format_P016, F::Format_YUV420P10}) {
    for (const bool full : {false, true}) {
      for (const auto matrix : {F::ColorSpace_BT601, F::ColorSpace_BT709, F::ColorSpace_BT2020}) {
        const auto name = QString("parity-%1-%2-%3").arg(int(format)).arg(full).arg(int(matrix));
        if (!run(name, format, {66, 50}, {53, 39}, full, matrix, false, true, 1)) {
          std::cerr << "Conversion fixture failed: " << name.toStdString() << '\n'; return 1;
        }
      }
    }
  }
  if (!run("camera-p010", F::Format_P010, {66, 50}, {66, 50}, false, F::ColorSpace_BT709, true, false, 1)) return 1;
  for (const auto format : {F::Format_NV12, F::Format_YUV420P, F::Format_P010, F::Format_UYVY}) {
    if (!run(QString("1080p-%1").arg(int(format)), format, {1920, 1080}, {1920, 1080}, false, F::ColorSpace_BT709, false, false, 20)) return 1;
  }
  if (!run("4k-nv12-to-1080p", F::Format_NV12, {3840, 2160}, {1920, 1080}, true, F::ColorSpace_BT709, false, false, 20)) return 1;
  std::cout << QJsonDocument(QJsonObject{{"cases", records}}).toJson(QJsonDocument::Indented).constData();
  return 0;
}
