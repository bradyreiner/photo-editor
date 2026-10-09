#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QThread>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QElapsedTimer>
#include <array>
#include <functional>

inline int run_gaussian_capture_setup_proof(QQuickWindow* window, const QString& output, int timeout_ms,
                                          const QString& capture_state = {}) {
  if (QFileInfo::exists(output) || !QDir().mkpath(output)) return 1;
  const bool native_platform = QGuiApplication::platformName() != QStringLiteral("offscreen");
  const auto wait = [&](const std::function<bool()>& done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
    }
    return done();
  };
  // A selected state supports isolated capture diagnostics.
  // This is window readback evidence, not an operating-system screenshot or
  // a frame-timing measurement.
  QVariantList capture_geometry;
  const auto capture = [&](const QString& name) {
    if (!capture_state.isEmpty() && name != capture_state) return true;
    const auto frame = window->grabWindow();
    const QSize expected(qRound(window->width() * window->devicePixelRatio()),
                         qRound(window->height() * window->devicePixelRatio()));
    capture_geometry.append(QVariantMap{{"name", name}, {"width", frame.width()},
        {"height", frame.height()}, {"expectedWidth", expected.width()}, {"expectedHeight", expected.height()}});
    const bool saved = frame.save(QDir(output).filePath(name));
    return saved && frame.size() == expected;
  };
  window->show(); window->raise(); window->requestActivate();
  if (native_platform && !wait([&] { return window->isActive(); })) {
    QFile failure(QDir(output).filePath("foreground-failure.json"));
    if (failure.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      failure.write(QJsonDocument::fromVariant(QVariantMap{{"active", window->isActive()},
          {"exposed", window->isExposed()}, {"platform", QGuiApplication::platformName()}}).toJson());
    }
    return 1;
  }
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
  auto* guidance = workspace->findChild<QQuickItem*>("sceneCaptureActionGuidance");
  if (!popup || !steps || !resolution || !budget || !start || !guidance ||
      !QMetaObject::invokeMethod(popup, "open")) return 1;
  workspace->setProperty("advancedCapture", true);
  bool passed = steps->property("value").toInt() == 30000 &&
      resolution->property("currentText") == "3200" && budget->property("maximumSplats").toInt() == 2000000 &&
      guidance->isVisible() && guidance->property("text").toString().contains("Choose overlapping photos") &&
      !start->isEnabled();
  bool eligible = native_platform;
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
      return resized.elapsed() >= 200 && window->size() == size && contained();
    }) && passed;
    eligible = eligible && window->isActive() && window->isExposed();
    passed = capture(QStringLiteral("capture-%1.png").arg(size.width())) && passed;
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
      frames->property("value").toInt() == 480 && start->property("enabled").toBool() &&
      !guidance->isVisible();
  QElapsedTimer video_layout; video_layout.start();
  wait([&] { return video_layout.elapsed() > 300; });
  const auto* video_content = popup->property("contentItem").value<QQuickItem*>();
  if (!video_content) return 1;
  const auto video_bounds = video_content->mapRectToScene(video_content->boundingRect()).adjusted(-1,-1,1,1);
  for (const auto* item : {frames, quality, start}) passed = passed && item && item->isVisible() && video_bounds.contains(item->mapRectToScene(item->boundingRect()));
  passed = capture("video-create.png") && passed;
  eligible = eligible && window->isActive() && window->isExposed();
  QMetaObject::invokeMethod(popup, "close");
  auto* photo_popup = workspace->findChild<QObject*>("scenePhotoControlsPopup");
  auto* photo_section = workspace->findChild<QQuickItem*>("scenePhotoSettingsSectionCombo");
  if (!photo_popup || !photo_section || !QMetaObject::invokeMethod(photo_popup, "open")) return 1;
  bool section_layouts = true;
  QVariantList photo_sections;
  const QStringList section_names{"Tone", "Focus", "Aperture", "Film", "Camera"};
  for (int index = 0; index < section_names.size(); ++index) {
    photo_section->setProperty("currentIndex", index);
    if (!QMetaObject::invokeMethod(photo_section, "activated", Q_ARG(int, index))) return 1;
    QElapsedTimer section_settle; section_settle.start();
    wait([&] { return section_settle.elapsed() >= 180; });
    auto* content = photo_popup->property("contentItem").value<QQuickItem*>();
    const QRectF window_bounds(0, 0, window->width(), window->height());
    const bool section_fits = content && content->isVisible() &&
        window_bounds.contains(content->mapRectToScene(content->boundingRect())) &&
        content->mapRectToScene(content->boundingRect()).contains(
            photo_section->mapRectToScene(photo_section->boundingRect())) &&
        photo_popup->property("page").toInt() == index;
    section_layouts = section_layouts && section_fits;
    auto* aperture = workspace->findChild<QQuickItem*>("sceneLensApertureSlider");
    auto* focus = workspace->findChild<QQuickItem*>("sceneLensFocusField");
    auto* capture_focus = workspace->findChild<QQuickItem*>("sceneUseCaptureFocusButton");
    section_layouts = section_layouts && aperture && focus && capture_focus &&
        aperture->isVisible() == (index == 2) && focus->isVisible() == (index == 1) &&
        !capture_focus->isEnabled();
    passed = capture(QStringLiteral("photo-settings-%1.png").arg(section_names[index].toLower())) && passed;
    photo_sections.append(QVariantMap{{"section", section_names[index]}, {"contained", section_fits}});
  }
  auto* render_details = workspace->findChild<QObject*>("sceneRenderDetailsToggle");
  const bool details_collapsed = !photo_popup->property("renderDetailsExpanded").toBool();
  if (!render_details || !QMetaObject::invokeMethod(render_details, "clicked")) return 1;
  const bool details_reachable = details_collapsed && photo_popup->property("renderDetailsExpanded").toBool();
  passed = details_reachable && capture("photo-settings-render-details.png") && passed;
  QMetaObject::invokeMethod(photo_popup, "close");
  QVariantList dialogs;
  for (const auto& name : {QStringLiteral("sceneSaveViewPopup"),
                           QStringLiteral("sceneCameraPathPopup"),
                           QStringLiteral("sceneTimedFilmPopup")}) {
    auto* dialog = workspace->findChild<QObject*>(name);
    if (!dialog || !QMetaObject::invokeMethod(dialog, "open")) return 1;
    QElapsedTimer dialog_settle; dialog_settle.start();
    wait([&] { return dialog_settle.elapsed() >= 180; });
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    const QRectF window_bounds(0, 0, window->width(), window->height());
    const bool dialog_fits = content && content->isVisible() &&
        window_bounds.contains(content->mapRectToScene(content->boundingRect()));
    section_layouts = section_layouts && dialog_fits;
    passed = capture(name + ".png") && passed;
    dialogs.append(QVariantMap{{"name", name}, {"contained", dialog_fits}});
    QMetaObject::invokeMethod(dialog, "close");
  }
  auto* layout_button = workspace->findChild<QObject*>("sceneWorkspaceLayoutButton");
  auto* navigation = window->findChild<QQuickItem*>("appNavigationBar");
  auto* preferences = workspace->findChild<QObject*>("sceneWorkspaceSettings");
  std::function<QQuickItem*(QQuickItem*, const QString&)> find_visual =
      [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems()) {
      if (auto* found = find_visual(child, name)) return found;
    }
    return nullptr;
  };
  const auto trigger_layout_item = [&](int index) {
    if (!layout_button || !QMetaObject::invokeMethod(layout_button, "openMenu")) return false;
    QQuickItem* item = nullptr;
    if (!wait([&] {
          item = find_visual(window->contentItem(), QStringLiteral("sceneWorkspaceLayoutButtonPopupItem%1").arg(index));
          return item && item->isVisible();
        })) return false;
    return QMetaObject::invokeMethod(item, "clicked");
  };
  const bool initially_immersive = workspace->property("immersive").toBool();
  bool modular_layout = navigation && preferences && initially_immersive &&
      trigger_layout_item(0) && wait([&] { return navigation->isVisible() &&
          !workspace->property("immersive").toBool(); });
  modular_layout = modular_layout && capture("scene-workspace-navigation.png") &&
      trigger_layout_item(2) && !workspace->property("previewStatusVisible").toBool() &&
      QMetaObject::invokeMethod(preferences, "sync");
  QSettings persisted;
  persisted.beginGroup("SceneWorkspace");
  // Qt Core Settings batches property writes. Wait for the durable values,
  // rather than treating sync() as a flush of its pending property timer.
  const bool layout_persisted = wait([&] {
    persisted.sync();
    return !persisted.value("immersive", true).toBool() &&
        !persisted.value("previewStatusVisible", true).toBool();
  });
  modular_layout = modular_layout && layout_persisted && trigger_layout_item(0) &&
      wait([&] { return !navigation->isVisible() && workspace->property("immersive").toBool(); });
  if (layout_button && QMetaObject::invokeMethod(layout_button, "openMenu")) {
    passed = capture("scene-layout-menu.png") && passed;
    QMetaObject::invokeMethod(layout_button, "closeMenu");
  } else modular_layout = false;
  passed = passed && modular_layout;
  passed = section_layouts && passed;
  passed = passed && (capture_state.isEmpty() || capture_geometry.size() == 1);
  eligible = eligible && window->isActive() && window->isExposed();
  const QVariantMap receipt{{"schema", "framehouse-gaussian-capture-setup-proof-v1"}, {"passed", passed},
      {"eligible", eligible}, {"defaultSteps", 30000}, {"defaultEdge", 3200}, {"defaultMaxSplats", 2000000},
      {"layouts", layouts}, {"videoSelectionAndExploreDefaults", passed},
      {"photoSections", photo_sections}, {"dialogs", dialogs},
      {"renderDetailsReachable", details_reachable},
      {"modularLayoutAndPersistence", modular_layout},
      {"layoutPreferencesPersisted", layout_persisted},
      {"proofMode", native_platform ? "native-active-window" : "qt-layout-render-only"},
      {"platform", QGuiApplication::platformName()}, {"devicePixelRatio", window->devicePixelRatio()},
      {"graphicsApi", static_cast<int>(window->rendererInterface()->graphicsApi())},
      {"captureGeometry", capture_geometry},
      {"captureOnlyState", capture_state},
      {"captureMethod", capture_state.isEmpty() ? "window-readback-sequence"
                                               : "single-window-readback-in-fresh-process"},
      {"scope", "actual-main-qml-configuration-and-popup-layout-not-reconstruction-quality"}};
  QFile file(QDir(output).filePath("receipt.json"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return 1;
  return passed && (!native_platform || eligible) ? 0 : 1;
}
