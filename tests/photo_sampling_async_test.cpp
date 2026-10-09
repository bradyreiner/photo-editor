#include "editor_preview_controller.h"
#include "decoded_image_cache.h"
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void source(EditorPreviewController& c, const QString& path, const QString& asset = QStringLiteral("sample")) {
    c.cancelPendingAssetScopedWork();
    c.file_path_ = path;
    c.asset_id_ = asset;
    c.hsl_channel_ = QStringLiteral("Greens");
  }
  static void crop(EditorPreviewController& c) {
    c.crop_rect_x_ = .5; c.crop_rect_y_ = 0.;
    c.crop_rect_width_ = .5; c.crop_rect_height_ = 1.;
  }
};
}
namespace {
bool wait(photo_editor::desktop::EditorPreviewController& c) {
  QElapsedTimer timer; timer.start();
  while (c.sourceSamplingBusy() && timer.elapsed() < 10000) {
    QCoreApplication::processEvents(); QThread::msleep(1);
  }
  QCoreApplication::processEvents();
  return !c.sourceSamplingBusy();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QTemporaryDir root;
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", root.path().toUtf8());
  QApplication app(argc, argv);
  using namespace photo_editor::desktop;
  QImage image(2400, 1600, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) line[x] = x < image.width() / 2 ? qRgb(220, 20, 20) : qRgb(20, 20, 220);
  }
  const auto path = root.filePath(QStringLiteral("sample.png"));
  if (!image.save(path)) return fail("source fixture failed");
  clear_decoded_image_cache();
  EditorPreviewController c;
  c.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(c, path);
  if (!c.selectColorMixerChannelAt(.8, .5) || !c.sourceSamplingBusy()) return fail("cold request must queue worker");
  if (c.hslChannel() != QStringLiteral("Greens")) return fail("cold request published prematurely");
  if (!wait(c) || c.hslChannel() != QStringLiteral("Blues")) return fail("cold blue sample incorrect");
  if (!c.selectColorMixerChannelAt(.2, .5) || c.sourceSamplingBusy() || c.hslChannel() != QStringLiteral("Reds")) return fail("warm red sample incorrect");
  EditorPreviewControllerTestAccess::crop(c);
  if (!c.selectColorMixerChannelAt(0., .5) || c.hslChannel() != QStringLiteral("Blues")) return fail("crop-edge patch included pixels outside crop");
  if (c.selectColorMixerChannelAt(NAN, .5)) return fail("nonfinite sample should reject");

  clear_decoded_image_cache();
  EditorPreviewController latest;
  latest.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(latest, path);
  latest.selectColorMixerChannelAt(.2, .5);
  latest.selectColorMixerChannelAt(.8, .5);
  if (!wait(latest) || latest.hslChannel() != QStringLiteral("Blues")) return fail("latest sample did not win");

  clear_decoded_image_cache();
  EditorPreviewController changed;
  changed.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(changed, path);
  changed.selectColorMixerChannelAt(.8, .5);
  EditorPreviewControllerTestAccess::source(changed, path, QStringLiteral("replacement"));
  if (!wait(changed) || changed.hslChannel() != QStringLiteral("Greens")) return fail("stale asset sample published");

  clear_decoded_image_cache();
  EditorPreviewController destroyed;
  destroyed.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(destroyed, path);
  destroyed.setColorToolSamplingTarget(QStringLiteral("color-replace-source"));
  destroyed.sampleColorToolAt(.8, .5);
  destroyed.setColorToolSamplingTarget(QString());
  if (!wait(destroyed) || destroyed.colorReplacementEnabled()) return fail("cancelled color sample applied edits");

  clear_decoded_image_cache();
  EditorPreviewController cancelled;
  cancelled.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(cancelled, path);
  cancelled.selectColorMixerChannelAt(.8, .5);
  cancelled.cancelColorMixerSample();
  if (!wait(cancelled) || cancelled.hslChannel() != QStringLiteral("Greens"))
    return fail("released mixer gesture published a late channel");

  // The asynchronous decoder and bounded patch must retain the original
  // white-balance calculation, including clipping at a crop edge.
  const auto neutral_path = root.filePath(QStringLiteral("warm-neutral.png"));
  QImage neutral(400, 300, QImage::Format_RGB32);
  neutral.fill(qRgb(216, 180, 160));
  if (!neutral.save(neutral_path)) return fail("white balance fixture failed");
  EditorPreviewController balance;
  balance.setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(balance, neutral_path);
  EditorPreviewControllerTestAccess::crop(balance);
  balance.setWhiteBalanceSampling(true);
  balance.sampleWhiteBalanceAt(0., 0.);
  const auto linear = [](double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
  };
  const auto red = linear(216. / 255.);
  const auto green = linear(180. / 255.);
  const auto blue = linear(160. / 255.);
  const auto expected_temperature = std::log2((blue + 1e-6) / (red + 1e-6)) * 55.;
  const auto expected_tint = std::log2((green + 1e-6) / ((red + blue) * .5)) * 60.;
  if (!wait(balance) || balance.whiteBalanceSampling() ||
      std::abs(balance.temperature() - expected_temperature) > .001 ||
      std::abs(balance.tint() - expected_tint) > .001)
    return fail("cold crop-edge white balance changed its pixel calculation");

  // Worker lifetime is independent of the controller; deleting while decoding
  // must not call a removed QObject or capture its mutable editorial state.
  clear_decoded_image_cache();
  auto* transient = new EditorPreviewController;
  transient->setEditAccessPolicy([] { return true; });
  EditorPreviewControllerTestAccess::source(*transient, path);
  transient->selectColorMixerChannelAt(.8, .5);
  delete transient;
  QThreadPool::globalInstance()->waitForDone();
  return 0;
}
