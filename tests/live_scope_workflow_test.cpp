#include "editor_preview_controller.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath("app-data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  using namespace photo_editor::desktop;
  EditorPreviewController editor;
  const auto wait = [&](const auto& predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 45000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(1);
    }
    return predicate();
  };
  const auto fail = [&](const char* message) {
    std::cerr << message << ": " << editor.status().toStdString()
              << " scope_samples=" << editor.histogramSampleCount()
              << " scope_generation=" << editor.scopeFrame().value(QStringLiteral("generation")).toInt() << '\n';
    QThreadPool::globalInstance()->waitForDone();
    return 1;
  };
  QImage image(1024, 640, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      image.setPixelColor(x, y, QColor(40 + x * 160 / image.width(), 50 + y * 140 / image.height(), 90));
  const auto path = runtime.filePath("scope.png");
  if (!runtime.isValid() || !image.save(path)) return 1;
  editor.setPreviewTargetEdge(1024);
  editor.setScopeSamplingMode(QStringLiteral("Fast"));
  editor.setScopeRegion(QRectF(0, 0, 1, 1));
  editor.setScopesVisible(true);
  editor.loadAsset(QStringLiteral("scope-workflow"), path, {}, {}, {}, {}, {});
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive() && editor.histogramSampleCount() > 0; }))
    return fail("Initial Fast scopes did not settle");
  const auto recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto fast_samples = editor.histogramSampleCount();
  editor.setScopeSamplingMode(QStringLiteral("Precise"));
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive() && editor.histogramSampleCount() > fast_samples; }))
    return fail("Precise must replace cached Fast scopes with more actual samples");
  if (editor.scopeFrame().value(QStringLiteral("levels")).toInt() != 128)
    return fail("Precise must publish higher waveform resolution");
  const auto whole_samples = editor.histogramSampleCount();
  editor.setScopeRegion(QRectF(0.25, 0.25, 0.5, 0.5));
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive() && editor.histogramSampleCount() < whole_samples; }))
    return fail("Region analysis must replace the whole-image sample set");
  if (recipe != editor.currentAssetBoundAdjustmentPatchJson())
    return fail("Scope sampling and region changes must not alter the recipe");
  editor.setScopesFrozen(true);
  const auto frozen = editor.scopeFrame();
  editor.setExposure(0.6);
  if (!wait([&]() { return !editor.processing(); }) || editor.scopeFrame() != frozen)
    return fail("Frozen scopes must retain their entire snapshot during edits");
  editor.setScopesFrozen(false);
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive() && editor.scopeFrame() != frozen; }))
    return fail("Resuming scopes must recompute without another image edit");
  const auto before_drag = editor.scopeFrame();
  editor.beginCoalescedEdit();
  editor.setExposure(0.9);
  const auto live = wait([&]() { return editor.scopeFrame().value(QStringLiteral("generation")) != before_drag.value(QStringLiteral("generation")); });
  editor.endCoalescedEdit();
  if (!live) return fail("Scopes must publish while the editing gesture is still open");
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive(); })) return fail("Final scopes did not refine");
  editor.setScopeSamplingMode(QStringLiteral("Fast"));
  editor.setScopeRegion(QRectF(0, 0, 1, 1));
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive(); })) return fail("Fast scopes did not settle");
  int live_updates = 0;
  auto last_scope_generation = editor.scopeFrame().value(QStringLiteral("generation")).toInt();
  const auto connection = QObject::connect(&editor, &EditorPreviewController::histogramChanged,
      &editor, [&]() {
        const auto generation = editor.scopeFrame().value(QStringLiteral("generation")).toInt();
        if (generation > last_scope_generation) {
          last_scope_generation = generation;
          ++live_updates;
        }
      });
  editor.beginCoalescedEdit();
  QElapsedTimer gesture; gesture.start();
  for (int i = 0; i < 30; ++i) {
    editor.setExposure(0.95 + i * 0.01);
    QElapsedTimer tick; tick.start();
    while (tick.elapsed() < 33) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      QThread::msleep(1);
    }
  }
  QObject::disconnect(connection);
  editor.endCoalescedEdit();
  std::cout << "continuous_gesture_ms=" << gesture.elapsed() << " scope_publications=" << live_updates << '\n';
  if (live_updates < 2) return fail("Continuous edits must not starve scopes until pointer release");
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive(); })) return fail("Continuous scopes did not settle");
  QImage other(64, 32, QImage::Format_RGB32);
  other.fill(QColor(30, 60, 180));
  const auto other_path = runtime.filePath("other.png");
  if (!other.save(other_path)) return 1;
  bool stale_asset_publication = false;
  const auto asset_connection = QObject::connect(&editor, &EditorPreviewController::histogramChanged,
      &editor, [&]() {
        if (editor.assetId() == QStringLiteral("other") && editor.histogramSampleCount() > 64 * 32)
          stale_asset_publication = true;
      });
  editor.setScopeSamplingMode(QStringLiteral("Precise"));
  editor.loadAsset(QStringLiteral("other"), other_path, {}, {}, {}, {}, {});
  if (!wait([&]() { return !editor.processing() && !editor.histogramUpdateActive(); }) ||
      editor.histogramSampleCount() != 64 * 32 || stale_asset_publication)
    return fail("Changing assets must discard pending scopes from the previous photo");
  QObject::disconnect(asset_connection);
  QThreadPool::globalInstance()->waitForDone();
  std::cout << "Actual sampling budgets, region, recipe isolation, freeze/resume and live gesture publication passed\n";
}
