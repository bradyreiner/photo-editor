#pragma once

// Real Main.qml navigation, including a trained scene. This deliberately does
// not assign currentWorkspace, prewarm pages, or count an inactive window as a
// foreground performance pass. The existing wrapper supplies RSS/log artifacts.
#include "gaussian_scene_controller.h"
#include "gaussian_scene_viewport.h"
#include "observability.h"
#include <QCryptographicHash>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <atomic>

inline int run_workspace_switch_proof(QQuickWindow* window,
    photo_editor::desktop::GaussianSceneController& scene, const QString& project,
    const QString& output, int iterations, int timeout_ms, bool capture) {
  using photo_editor::desktop::GaussianSceneViewport;
  QJsonArray routes;
  QString error;
  bool eligible = true;
  int foreground_samples = 0;
  auto foreground = [&] {
    ++foreground_samples;
    const bool active = window->isActive() && window->isExposed() &&
        QGuiApplication::applicationState() == Qt::ApplicationActive;
    eligible = eligible && active;
    return active;
  };
  auto wait = [&](const std::function<bool()>& done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
      foreground();
      QThread::msleep(1);
    }
    foreground();
    return done();
  };
  if (project.isEmpty() || !QDir().mkpath(output)) return EXIT_FAILURE;
  window->show(); window->raise(); window->requestActivate();
  // Activation is setup, not part of route timings. Keep unsuccessful activation
  // visible in the receipt while still gathering diagnostic-only measurements.
  wait([&] { return window->isActive(); });
  eligible = true;
  foreground_samples = 0;
  const auto swaps = std::make_shared<std::atomic_int>(0);
  const auto frame_connection = QObject::connect(window, &QQuickWindow::frameSwapped,
      window, [swaps] { swaps->fetch_add(1); }, Qt::DirectConnection);
  QElapsedTimer elapsed; elapsed.start();
  QElapsedTimer heartbeat_clock; heartbeat_clock.start();
  qint64 max_gap_ms = 0, last_tick_ms = 0;
  QTimer heartbeat;
  heartbeat.setInterval(8);
  QObject::connect(&heartbeat, &QTimer::timeout, window, [&] {
    const auto now = heartbeat_clock.elapsed();
    max_gap_ms = std::max(max_gap_ms, now - last_tick_ms);
    last_tick_ms = now;
    foreground();
  });
  heartbeat.start();
  QElapsedTimer load_timer; load_timer.start();
  if (!scene.openProject(project).value("ok").toBool()) error = "Cannot open the supplied trained scene.";
  GaussianSceneViewport* viewport = nullptr;
  auto route = [&](int index, const QString& name, int iteration) {
    if (!error.isEmpty()) return;
    const auto before = swaps->load();
    QElapsedTimer timer; timer.start();
    QVariant accepted;
    if (!QMetaObject::invokeMethod(window, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, accepted),
        Q_ARG(QVariant, QVariant(index))) || !accepted.toBool()) {
      error = "Navigation rejected " + name; return;
    }
    if (!wait([&] {
      if (window->property("currentWorkspace").toInt() != index) return false;
      QVariant item;
      if (!QMetaObject::invokeMethod(window, "activeWorkspaceItem", Q_RETURN_ARG(QVariant, item))) return false;
      auto* active = qobject_cast<QQuickItem*>(item.value<QObject*>());
      return active && active->isVisible();
    })) { error = "Page did not become visible: " + name; return; }
    const auto visible_ms = timer.elapsed();
    if (!wait([&] { return !window->property("workspaceTransitionActive").toBool(); })) {
      error = "Navigation overlay did not release input: " + name; return;
    }
    const auto released_ms = timer.elapsed();
    const auto ready_swaps = swaps->load();
    window->update();
    if (!wait([&] { return swaps->load() > ready_swaps; })) {
      error = "No presented frame after navigation: " + name; return;
    }
    auto* current_viewport = window->findChild<GaussianSceneViewport*>("gaussianSceneViewport");
    if (name == "Scene") {
      if (viewport && viewport != current_viewport) { error = "Returning to Scene recreated the viewport."; return; }
      viewport = current_viewport;
      if (!viewport || !wait([&] { return viewport->status().value("frameAvailable").toBool() &&
          viewport->status().value("previewPhase") == "photo"; })) {
        error = "Scene never produced a native frame."; return;
      }
      const auto scene_swaps = swaps->load();
      window->update();
      if (!wait([&] { return swaps->load() > scene_swaps; })) {
        error = "Scene frame was not presented."; return;
      }
    }
    routes.append(QJsonObject{{"page", name}, {"iteration", iteration},
        {"visibleMs", visible_ms}, {"inputReleasedMs", released_ms},
        {"presentedReadyMs", timer.elapsed()}, {"frameSwaps", swaps->load() - before},
        {"foreground", foreground()}});
  };
  const auto scene_index = window->property("sceneWorkspaceIndex").toInt();
  route(scene_index, "Scene", -1);
  const auto scene_load_ms = load_timer.elapsed();
  const auto points = viewport ? viewport->status().value("pointCount").toLongLong() : 0;
  const auto initial_metrics = viewport ? viewport->status().value("metrics").toMap() : QVariantMap{};
  QJsonArray captures;
  auto screenshot = [&](const QString& name) {
    if (!capture || !error.isEmpty()) return;
    const auto path = QDir(output).filePath(name + ".png");
    if (!window->grabWindow().save(path)) error = "Cannot capture " + name;
    else captures.append(path);
  };
  // Warm navigation must preserve the scene's orbit/camera, not just its object.
  QVariantMap camera;
  if (viewport && error.isEmpty()) {
    const auto published = viewport->status().value("publishedFrames").toInt();
    if (capture) viewport->beginNavigation();
    viewport->orbit(0.025, -0.01);
    if (capture) {
      if (!wait([&] { return viewport->status().value("publishedPhase") == "live"; }))
        error = "Live navigation did not publish a draft.";
      screenshot("scene-live");
      viewport->endNavigation();
    }
    if (!wait([&] { return viewport->status().value("publishedFrames").toInt() > published &&
        viewport->status().value("previewPhase") == "photo"; }))
      error = "Orbit did not produce a new Gaussian frame.";
    camera = viewport->status().value("metrics").toMap().value("camera").toMap();
    if (camera.isEmpty()) error = "Scene camera evidence is missing.";
  }
  auto export_hash = [&](const QString& name) {
    if (!capture || !viewport || !error.isEmpty()) return QByteArray{};
    const auto path = QDir(output).filePath(name + ".png");
    if (!viewport->exportPhoto(path, 800).value("ok").toBool() ||
        !wait([&] { return !viewport->status().value("exporting").toBool(); }) ||
        viewport->status().value("lastExport").toString() != path) {
      error = "Novel-view export failed: " + name; return QByteArray{};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = "Export cannot be read."; return QByteArray{}; }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
  };
  const auto before_hash = export_hash("photo-before");
  screenshot("scene-before");
  const QStringList pages{"Library", "Edit", "Carousel", "Timeline", "Export", "Scene"};
  const QStringList properties{"libraryWorkspaceIndex", "editWorkspaceIndex", "carouselWorkspaceIndex",
      "timelineWorkspaceIndex", "exportWorkspaceIndex", "sceneWorkspaceIndex"};
  for (int iteration = 0; iteration < iterations && error.isEmpty(); ++iteration) {
    for (qsizetype page = 0; page < pages.size(); ++page)
      route(window->property(properties[page].toUtf8().constData()).toInt(), pages[page], iteration);
  }
  const bool camera_preserved = viewport && !camera.isEmpty() &&
      viewport->status().value("metrics").toMap().value("camera").toMap() == camera;
  if (!camera_preserved && error.isEmpty()) error = "Navigation changed the scene camera.";
  const auto after_hash = export_hash("photo-after");
  if (capture && (before_hash.isEmpty() || before_hash != after_hash) && error.isEmpty())
    error = "Novel-view export changed after navigating away and back.";
  screenshot("scene-after");
  bool immersive_ok = false;
  if (capture && error.isEmpty()) {
    auto* surface = window->findChild<QQuickItem*>("sceneFullScreenSurface");
    auto* navigation = window->findChild<QQuickItem*>("appNavigationBar");
    immersive_ok = surface && surface->isVisible() && navigation && !navigation->isVisible() &&
        surface->width() >= window->width() - 2 && surface->height() >= window->height() - 2;
    if (!immersive_ok) error = "Scene is not an immersive full-window viewport.";
    auto* options = window->findChild<QObject*>("sceneOptionsButton");
    auto* popup = window->findChild<QObject*>("sceneControlsPopup");
    if (!options || !popup || !QMetaObject::invokeMethod(options, "clicked") ||
        !wait([&] { return popup->property("visible").toBool(); }))
      error = "Scene controls overlay did not open.";
    screenshot("scene-options");
    if (popup) QMetaObject::invokeMethod(popup, "close");
  }
  bool replacement_ok = false;
  if (capture && error.isEmpty()) {
    auto navigate = [&](int index) {
      QVariant accepted;
      return QMetaObject::invokeMethod(window, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, accepted),
          Q_ARG(QVariant, QVariant(index))) && accepted.toBool();
    };
    // No event processing between clicks: a request to stay must cancel the
    // pending route, and a later different destination must supersede it.
    const auto library = window->property("libraryWorkspaceIndex").toInt();
    const auto edit = window->property("editWorkspaceIndex").toInt();
    bool clicks = navigate(library) && navigate(scene_index);
    QElapsedTimer settle; settle.start();
    wait([&] { return settle.elapsed() >= 250; });
    replacement_ok = clicks && window->property("currentWorkspace").toInt() == scene_index &&
        !window->property("workspaceTransitionActive").toBool();
    clicks = navigate(library) && navigate(edit);
    replacement_ok = replacement_ok && clicks && wait([&] {
      return window->property("currentWorkspace").toInt() == edit &&
          !window->property("workspaceTransitionActive").toBool();
    });
    if (replacement_ok) route(scene_index, "Scene", iterations);
    if (!replacement_ok) error = "Rapid navigation did not honor the latest destination.";
  }
  bool photo_settings_ok = false;
  QByteArray adjusted_hash;
  if (capture && viewport && error.isEmpty()) {
    auto* button = window->findChild<QObject*>("scenePhotoSettingsButton");
    auto* popup = window->findChild<QObject*>("scenePhotoControlsPopup");
    auto* exposure = window->findChild<QObject*>("scenePhotoExposureSlider");
    auto* focal = window->findChild<QObject*>("scenePhotoFocalSlider");
    auto* look = window->findChild<QObject*>("scenePhotoLookCombo");
    if (!button || !popup || !exposure || !focal || !look || !QMetaObject::invokeMethod(button, "clicked") ||
        !wait([&] { return popup->property("visible").toBool(); })) {
      error = "Photographic controls did not open.";
    } else {
      const auto before_photo = viewport->status().value("metrics").toMap().value("camera").toMap();
      exposure->setProperty("value", 0.75);
      bool edited = QMetaObject::invokeMethod(exposure, "moved");
      popup->setProperty("page", 1);
      focal->setProperty("value", 1.25);
      edited = QMetaObject::invokeMethod(focal, "moved") && edited;
      popup->setProperty("page", 0);
      look->setProperty("currentIndex", 1);
      edited = QMetaObject::invokeMethod(look, "activated", Q_ARG(int, 1)) && edited;
      const photo_editor::desktop::GaussianPhotoSettings expected{1.25F, 0.75F, 1};
      photo_settings_ok = edited && wait([&] {
        return viewport->status().value("previewPhase") == "photo" &&
            viewport->status().value("metrics").toMap().value("camera").toMap().value("photo").toMap() == expected.description();
      });
      const auto processed = viewport->status().value("metrics").toMap().value("camera").toMap();
      photo_settings_ok = photo_settings_ok && processed.value("position") == before_photo.value("position") &&
          processed.value("forward") == before_photo.value("forward");
      screenshot("scene-photo-settings");
      QMetaObject::invokeMethod(popup, "close");
      adjusted_hash = export_hash("photo-adjusted");
      photo_settings_ok = photo_settings_ok && !adjusted_hash.isEmpty() && adjusted_hash != after_hash &&
          viewport->status().value("lastExportCamera").toMap().value("photo").toMap() == expected.description();
      screenshot("scene-photo-adjusted");
      QMetaObject::invokeMethod(popup, "open");
      route(window->property("libraryWorkspaceIndex").toInt(), "Library", iterations + 1);
      photo_settings_ok = photo_settings_ok && !popup->property("visible").toBool();
      route(scene_index, "Scene", iterations + 1);
      photo_settings_ok = photo_settings_ok && viewport->status().value("photoSettings").toMap() == expected.description() &&
          export_hash("photo-adjusted-return") == adjusted_hash;
      if (!photo_settings_ok && error.isEmpty()) error = "Photographic control, camera or export consistency failed.";
    }
  }
  bool lens_ok = false;
  QByteArray lens_hash;
  double lens_jpeg_psnr = 0;
  if (capture && viewport && error.isEmpty()) {
    auto* popup = window->findChild<QObject*>("scenePhotoControlsPopup");
    auto* focus = window->findChild<QObject*>("sceneLensFocusField");
    auto* aperture = window->findChild<QObject*>("sceneLensApertureSlider");
    auto* shape = window->findChild<QObject*>("sceneLensShapeCombo");
    auto* quality = window->findChild<QObject*>("sceneLensQualityCombo");
    auto* scroll = window->findChild<QObject*>("scenePhotoControlsScroll");
    const float distance = std::clamp(viewport->status().value("sceneFocusDistance").toFloat(), 0.01F, 1e8F);
    if (!popup || !focus || !aperture || !shape || !quality || !scroll) {
      error = "Thin-lens controls are missing.";
    } else {
      QMetaObject::invokeMethod(popup, "open");
      popup->setProperty("page", 1);
      focus->setProperty("text", QString::number(distance, 'g', 9));
      lens_ok = QMetaObject::invokeMethod(focus, "textEdited") && QMetaObject::invokeMethod(focus, "editingFinished");
      popup->setProperty("page", 2);
      aperture->setProperty("value", distance * 0.025F);
      lens_ok = QMetaObject::invokeMethod(aperture, "moved") && lens_ok;
      shape->setProperty("currentIndex", 2);
      lens_ok = QMetaObject::invokeMethod(shape, "activated", Q_ARG(int, 2)) && lens_ok;
      quality->setProperty("currentIndex", 0);
      lens_ok = QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 0)) && lens_ok;
      lens_ok = wait([&] {
        const auto state = viewport->status();
        return state.value("previewPhase") == "refining" && state.value("refinementSamplesTotal").toInt() == 16 &&
            state.value("refinementSamplesCompleted").toInt() >= 2 && state.value("refinementSamplesCompleted").toInt() < 16;
      }) && lens_ok;
      screenshot("scene-lens-refining");
      lens_ok = wait([&] {
        const auto metrics = viewport->status().value("metrics").toMap();
        return viewport->status().value("previewPhase") == "photo" && metrics.value("apertureSamples").toInt() == 16 &&
            metrics.value("lensModel") == "thin-lens-scene-units-v1";
      }) && lens_ok;
      const photo_editor::desktop::GaussianLensSettings expected{distance, distance * 0.025F, 7, 0, 0, 16};
      lens_ok = lens_ok && viewport->status().value("lensSettings").toMap() == expected.description();
      // Merely leaving a rounded display field must not rewrite stored focus.
      lens_ok = QMetaObject::invokeMethod(focus, "editingFinished") && lens_ok &&
          viewport->status().value("lensSettings").toMap() == expected.description();
      scroll->setProperty("contentY", std::max(0.0, scroll->property("contentHeight").toDouble() - scroll->property("height").toDouble()));
      screenshot("scene-lens-controls");
      popup->setProperty("page", 1);
      screenshot("scene-lens-focus");
      QMetaObject::invokeMethod(popup, "close");
      screenshot("scene-lens-resolved");
      lens_hash = export_hash("photo-lens");
      route(window->property("libraryWorkspaceIndex").toInt(), "Library", iterations + 2);
      route(scene_index, "Scene", iterations + 2);
      lens_ok = lens_ok && !lens_hash.isEmpty() && lens_hash != adjusted_hash &&
          viewport->status().value("lensSettings").toMap() == expected.description() &&
          viewport->status().value("lastExportCamera").toMap().value("photo").toMap().value("lens").toMap() == expected.description() &&
          export_hash("photo-lens-repeat") == lens_hash;
      const auto jpeg_path = QDir(output).filePath("photo-lens.jpg");
      lens_ok = viewport->exportPhoto(jpeg_path, 800).value("ok").toBool() &&
          wait([&] { return !viewport->status().value("exporting").toBool(); }) && lens_ok;
      const QImage jpeg(jpeg_path), png(QDir(output).filePath("photo-lens.png"));
      if (!jpeg.isNull() && jpeg.size() == png.size() && jpeg.colorSpace() == QColorSpace(QColorSpace::SRgb) &&
          viewport->status().value("lastExport").toString() == jpeg_path) {
        double squared = 0;
        for (int y = 0; y < jpeg.height(); ++y) for (int x = 0; x < jpeg.width(); ++x) {
          const auto a = jpeg.pixelColor(x, y), b = png.pixelColor(x, y);
          for (const int delta : {a.red() - b.red(), a.green() - b.green(), a.blue() - b.blue()}) squared += delta * delta;
        }
        lens_jpeg_psnr = 10 * std::log10(255.0 * 255.0 / std::max(1e-12, squared / (jpeg.width() * jpeg.height() * 3.0)));
        lens_ok = lens_ok && lens_jpeg_psnr >= 35;
      } else lens_ok = false;
      if (!lens_ok && error.isEmpty()) error = "Thin-lens UI, resolved frame or export consistency failed.";
    }
  }
  bool film_ok = false;
  QByteArray film_hash;
  if (capture && viewport && error.isEmpty()) {
    auto* popup = window->findChild<QObject*>("scenePhotoControlsPopup");
    auto* grain = window->findChild<QObject*>("sceneFilmGrainSlider");
    auto* size = window->findChild<QObject*>("sceneFilmGrainSizeSlider");
    auto* halo = window->findChild<QObject*>("sceneFilmHalationSlider");
    auto* radius = window->findChild<QObject*>("sceneFilmRadiusSlider");
    auto* threshold = window->findChild<QObject*>("sceneFilmThresholdSlider");
    if (!popup || !grain || !size || !halo || !radius || !threshold) error = "Film controls are missing.";
    else {
      auto expected = photo_editor::desktop::gaussian_photo_settings(viewport->status().value("photoSettings").toMap());
      QMetaObject::invokeMethod(popup, "open"); popup->setProperty("page", 3);
      const auto move = [](QObject* control, double value) {
        control->setProperty("value",value); return QMetaObject::invokeMethod(control,"moved");
      };
      film_ok = expected && move(grain,0.6) && move(size,3.0);
      screenshot("scene-film-grain-controls");
      popup->setProperty("halationPage",true);
      film_ok = move(halo,0.5) && move(radius,8.0) && move(threshold,0.3) && film_ok;
      if (expected) expected->film = {0.6F,3,0.5F,8,0.3F,1};
      film_ok = film_ok && wait([&] {
        const auto state = viewport->status();
        return state.value("previewPhase") == "photo" && state.value("metrics").toMap().value("filmEffectsApplied").toBool() &&
            state.value("photoSettings").toMap() == expected->description();
      });
      screenshot("scene-film-halation-controls");
      QMetaObject::invokeMethod(popup,"close"); screenshot("scene-film-resolved");
      film_hash = export_hash("photo-film");
      film_ok = film_ok && !film_hash.isEmpty() && film_hash != lens_hash &&
          viewport->status().value("lastExportCamera").toMap().value("photo").toMap() == expected->description();
      route(window->property("libraryWorkspaceIndex").toInt(), "Library", iterations+3);
      route(scene_index,"Scene",iterations+3);
      film_ok = film_ok && viewport->status().value("photoSettings").toMap() == expected->description() &&
          export_hash("photo-film-repeat") == film_hash;
      const auto jpeg_path = QDir(output).filePath("photo-film.jpg");
      film_ok = viewport->exportPhoto(jpeg_path,800).value("ok").toBool() &&
          wait([&] { return !viewport->status().value("exporting").toBool(); }) && film_ok;
      const QImage jpeg(jpeg_path), png(QDir(output).filePath("photo-film.png"));
      film_ok = film_ok && !jpeg.isNull() && jpeg.size() == png.size() &&
          jpeg.colorSpace() == QColorSpace(QColorSpace::SRgb) && viewport->status().value("lastExport").toString() == jpeg_path;
      if (!film_ok && error.isEmpty()) error = "Film controls, persistence or repeat export failed.";
    }
  }
  heartbeat.stop();
  QObject::disconnect(frame_connection);
  QMetaObject::invokeMethod(window, "flushQmlFrameSummary");
  const QJsonObject receipt{{"schema", "framehouse-workspace-switch-proof-v2"},
      {"setup", "isolated-settings-onboarding-complete-auto-preview"},
      {"eligible", eligible && foreground_samples > 0}, {"error", error},
      {"foregroundSamples", foreground_samples}, {"routes", routes}, {"totalMs", elapsed.elapsed()},
      {"maxHeartbeatGapMs", max_gap_ms}, {"sceneLoadMs", scene_load_ms}, {"scenePoints", points},
      {"sceneMetrics", QJsonObject::fromVariantMap(initial_metrics)}, {"captures", captures},
      {"cameraPreserved", camera_preserved}, {"photoBeforeSha256", QString::fromLatin1(before_hash)},
      {"photoAfterSha256", QString::fromLatin1(after_hash)},
      {"rapidNavigationChecked", capture}, {"rapidNavigationPassed", replacement_ok},
      {"immersiveChecked", capture}, {"immersivePassed", immersive_ok},
      {"photoSettingsChecked", capture}, {"photoSettingsPassed", photo_settings_ok},
      {"photoAdjustedSha256", QString::fromLatin1(adjusted_hash)},
      {"lensChecked", capture}, {"lensPassed", lens_ok}, {"lensPhotoSha256", QString::fromLatin1(lens_hash)},
      {"lensJpegPsnr", lens_jpeg_psnr},
      {"filmChecked", capture}, {"filmPassed", film_ok}, {"filmPhotoSha256", QString::fromLatin1(film_hash)},
      {"qmlDiskCacheDisabled", qEnvironmentVariable("QML_DISABLE_DISK_CACHE") == "1"},
      {"limits", "Single-photo catalog, retained trained scene; frameSwapped is not input-to-photon."}};
  QFile file(QDir(output).filePath("receipt.json"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(QJsonDocument(receipt).toJson()) < 0)
    return EXIT_FAILURE;
  file.close();
  photo_editor::desktop::observability::logInfo(photoEditorPerfLog(),
      "benchmark.workspace_switch.completed", {{"eligible", eligible}, {"error", error},
      {"route_count", routes.size()}, {"elapsed_ms", elapsed.elapsed()}});
  std::cout << QJsonDocument(receipt).toJson().constData();
  return error.isEmpty() && eligible ? EXIT_SUCCESS : EXIT_FAILURE;
}
