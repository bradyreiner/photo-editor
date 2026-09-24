#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QThread>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <array>
#include <functional>

inline int run_gaussian_capture_setup_proof(QQuickWindow* window, const QString& output, int timeout_ms) {
  if (QFileInfo::exists(output) || !QDir().mkpath(output)) return 1;
  const auto wait = [&](const std::function<bool()>& done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return done();
  };
  window->show(); window->raise(); window->requestActivate();
  if (!wait([&] { return window->isActive(); })) return 1;
  QVariant accepted;
  if (!QMetaObject::invokeMethod(window, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, accepted),
      Q_ARG(QVariant, window->property("sceneWorkspaceIndex"))) || !accepted.toBool()) return 1;
  QObject* workspace = nullptr;
  if (!wait([&] { workspace = window->findChild<QObject*>("sceneWorkspace");
    return workspace && !window->property("workspaceTransitionActive").toBool(); })) return 1;
  auto* popup = workspace->findChild<QObject*>("sceneControlsPopup");
  auto* steps = workspace->findChild<QQuickItem*>("sceneTrainingSteps");
  auto* resolution = workspace->findChild<QQuickItem*>("sceneTrainingResolution");
  auto* budget = workspace->findChild<QQuickItem*>("sceneTrainingSplatBudget");
  auto* start = workspace->findChild<QQuickItem*>("sceneStartCapture");
  if (!popup || !steps || !resolution || !budget || !start ||
      !QMetaObject::invokeMethod(popup, "open")) return 1;
  workspace->setProperty("advancedCapture", true);
  bool passed = steps->property("value").toInt() == 30000 &&
      resolution->property("currentText") == "3200" && budget->property("maximumSplats").toInt() == 2000000;
  bool eligible = true;
  QVariantList layouts;
  for (const auto size : {QSize(1600, 900), QSize(1024, 760)}) {
    window->showNormal(); window->resize(size);
    QElapsedTimer resized; resized.start();
    auto* content = popup->property("contentItem").value<QQuickItem*>();
    if (!content) return 1;
    const auto contained = [&] {
      const auto bounds = content->mapRectToScene(content->boundingRect()).adjusted(-1, -1, 1, 1);
      for (const auto* item : {steps, resolution, budget, start})
        if (!item->isVisible() || !bounds.contains(item->mapRectToScene(item->boundingRect()))) return false;
      return true;
    };
    // Native resize is asynchronous. Require the requested logical geometry
    // and matching physical framebuffer, not a stale pre-resize screenshot.
    passed = wait([&] {
      window->update();
      return resized.elapsed() >= 200 && window->size() == size && contained() &&
          window->grabWindow().size() == QSize(qRound(size.width()*window->devicePixelRatio()),
              qRound(size.height()*window->devicePixelRatio()));
    }) && passed;
    eligible = eligible && window->isActive() && window->isExposed();
    passed = window->grabWindow().save(QDir(output).filePath(QStringLiteral("capture-%1.png").arg(size.width()))) && passed;
    layouts.append(QVariantMap{{"requestedWidth", size.width()}, {"actualWidth", window->width()},
        {"actualHeight", window->height()}, {"controlsContained", contained()}});
  }
  for (int index = 0; index < 3; ++index) {
    budget->setProperty("currentIndex", index);
    passed = budget->property("maximumSplats").toInt() == std::array<int, 3>{500000, 1000000, 2000000}[static_cast<size_t>(index)] && passed;
  }
  steps->setProperty("value", 10000); resolution->setProperty("currentIndex", 4);
  passed = steps->property("value").toInt() == 10000 && resolution->property("currentText") == "4096" && passed;
  // The file-picker/drop/launch handoff shares this real QML entry point.
  // Selection does not start training or require users to type an output path.
  if (!QMetaObject::invokeMethod(workspace, "prepareVideoInput", Q_ARG(QString, QString("/tmp/Room Walk.MOV")))) return 1;
  auto* mode = workspace->findChild<QObject*>("sceneCaptureMode");
  auto* input = workspace->findChild<QObject*>("sceneCapturePath");
  auto* project = workspace->findChild<QObject*>("sceneProjectPath");
  auto* frames = workspace->findChild<QQuickItem*>("sceneVideoFrameCount");
  auto* quality = workspace->findChild<QQuickItem*>("sceneCaptureQuality");
  passed = passed && mode && input && project && frames && quality &&
      mode->property("currentIndex").toInt() == 2 && input->property("text") == "/tmp/Room Walk.MOV" &&
      project->property("text").toString().contains("Looksy Scenes/Room Walk ") &&
      !workspace->property("advancedCapture").toBool() && steps->property("value").toInt() == 5000 &&
      resolution->property("currentText") == "1600" && budget->property("maximumSplats").toInt() == 500000 &&
      frames->property("value").toInt() == 480 && start->property("enabled").toBool();
  QElapsedTimer video_layout; video_layout.start();
  wait([&] { return video_layout.elapsed() > 300; });
  const auto* video_content = popup->property("contentItem").value<QQuickItem*>();
  if (!video_content) return 1;
  const auto video_bounds = video_content->mapRectToScene(video_content->boundingRect()).adjusted(-1,-1,1,1);
  for (const auto* item : {frames, quality, start}) passed = passed && item && item->isVisible() && video_bounds.contains(item->mapRectToScene(item->boundingRect()));
  passed = window->grabWindow().save(QDir(output).filePath("video-create.png")) && passed;
  eligible = eligible && window->isActive() && window->isExposed();
  const QVariantMap receipt{{"schema", "framehouse-gaussian-capture-setup-proof-v1"}, {"passed", passed},
      {"eligible", eligible}, {"defaultSteps", 30000}, {"defaultEdge", 3200}, {"defaultMaxSplats", 2000000},
      {"layouts", layouts}, {"videoSelectionAndExploreDefaults", passed},
      {"scope", "actual-main-qml-configuration-and-layout-not-reconstruction-quality"}};
  QFile file(QDir(output).filePath("receipt.json"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return 1;
  return passed && eligible ? 0 : 1;
}
