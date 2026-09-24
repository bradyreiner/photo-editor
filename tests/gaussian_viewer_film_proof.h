#pragma once
#include "gaussian_scene_controller.h"
#include "gaussian_scene_viewport.h"
#include <QQuickWindow>
#include <QThread>
#include <QJsonArray>
#include "native_window_test_input.h"
#include <iostream>

inline int run_gaussian_viewer_film_proof(QQuickWindow* window,
    photo_editor::desktop::GaussianSceneController& scene, const QString& project,
    const QString& output, int camera_index, int timeout_ms, int photo_edge = 0) {
  using namespace photo_editor::desktop;
  if (!QDir().mkpath(output)) return 1;
  bool eligible = true;
  const auto wait = [&](const std::function<bool()>& done) {
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < timeout_ms) {
      QCoreApplication::processEvents(QEventLoop::AllEvents,2);
      eligible = eligible && window->isActive() && window->isExposed() && QGuiApplication::applicationState() == Qt::ApplicationActive;
      QThread::msleep(1);
    }
    return done();
  };
  window->show(); window->raise(); window->requestActivate();
  QElapsedTimer activation; activation.start();
  wait([&] { return window->isActive() || activation.elapsed() > 5000; });
  if (!window->isActive()) {
    const QVariantMap failure{{"stage","foreground-window"},{"active",window->isActive()},
        {"exposed",window->isExposed()},{"applicationState",static_cast<int>(QGuiApplication::applicationState())}};
    QFile file(QDir(output).filePath("foreground-failure.json"));
    if (file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) file.write(QJsonDocument::fromVariant(failure).toJson());
    window->grabWindow().save(QDir(output).filePath("foreground-failure.png"));
    std::cerr << "Viewer proof needs an active foreground window.\n";
    return 1;
  }
  eligible = true;
  if (!scene.openProject(project).value("ok").toBool()) return 1;
  QVariant accepted;
  if (!QMetaObject::invokeMethod(window,"switchToWorkspaceIndex",Q_RETURN_ARG(QVariant,accepted),
      Q_ARG(QVariant,window->property("sceneWorkspaceIndex"))) || !accepted.toBool()) return 1;
  GaussianSceneViewport* viewport = nullptr;
  if (!wait([&] {
    viewport = window->findChild<GaussianSceneViewport*>("gaussianSceneViewport");
    return viewport && viewport->status().value("previewPhase") == "photo" && !window->property("workspaceTransitionActive").toBool();
  })) return 1;
  if (camera_index < 0 || camera_index >= viewport->status().value("cameras").toStringList().size()) return 1;
  viewport->selectCamera(camera_index);
  if (!wait([&] { return viewport->status().value("previewPhase") == "photo"; })) return 1;
  auto photo = *gaussian_photo_settings(viewport->status().value("photoSettings").toMap());
  const auto focus = viewport->status().value("sceneFocusDistance").toFloat();
  photo.lens = {focus,focus*0.015F,7,0,0,16}; photo.film = {0.3F,2,0.3F,4,0.8F,1};
  if (!viewport->setPhotoSettings(photo.description()).value("ok").toBool() ||
      !wait([&] { return viewport->status().value("previewPhase") == "photo"; })) return 1;
  auto* popup = window->findChild<QObject*>("scenePhotoControlsPopup");
  auto* grain = window->findChild<QObject*>("sceneFilmGrainSlider");
  if (!popup || !grain || !QMetaObject::invokeMethod(popup,"open")) return 1;
  popup->setProperty("page",1);
  auto* focus_button = window->findChild<QObject*>("scenePickFocusButton");
  auto* workspace = window->findChild<QObject*>("sceneWorkspace");
  if (!focus_button || !workspace || !wait([&] { return focus_button->property("visible").toBool(); }) ||
      !QMetaObject::invokeMethod(focus_button,"clicked") ||
      !wait([&] { return !popup->property("visible").toBool() && workspace->property("focusPickArmed").toBool(); })) return 1;
  if (!window->grabWindow().save(QDir(output).filePath("focus-pick-armed.png"))) return 1;
  const QPointF location = viewport->mapToScene({viewport->width()*0.5,viewport->height()*0.6});
  QElapsedTimer focus_timer; focus_timer.start();
  click_native_test_window(window,location.toPoint());
  const auto focus_finished = wait([&] { return focus_timer.elapsed() > 15000 || !workspace->property("actionError").toString().isEmpty() ||
      (!viewport->status().value("focusPicking").toBool() && !viewport->status().value("focusPickResult").toMap().isEmpty()); });
  if (!focus_finished || !viewport->status().value("focusPickResult").toMap().value("ok").toBool()) {
    const QVariantMap failure{{"stage","focus-pick"},{"status",viewport->status()},
        {"actionError",workspace->property("actionError")},{"armed",workspace->property("focusPickArmed")}};
    QFile failure_file(QDir(output).filePath("focus-failure.json"));
    if (failure_file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) failure_file.write(QJsonDocument::fromVariant(failure).toJson());
    window->grabWindow().save(QDir(output).filePath("focus-failure.png"));
    std::cerr << QJsonDocument::fromVariant(failure).toJson(QJsonDocument::Compact).constData() << '\n';
    return 1;
  }
  auto focus_proof = viewport->status().value("focusPickResult").toMap();
  focus_proof.insert("publishedMs",focus_timer.elapsed());
  const auto picked_photo = *gaussian_photo_settings(viewport->status().value("photoSettings").toMap());
  auto expected_photo = photo; expected_photo.lens.focus_distance = focus_proof.value("focusDistance").toFloat();
  if (picked_photo != expected_photo || workspace->property("focusPickArmed").toBool() ||
      !wait([&] { return viewport->status().value("previewPhase") == "photo"; }) ||
      !window->grabWindow().save(QDir(output).filePath("focus-picked-photo.png"))) return 1;
  if (!QMetaObject::invokeMethod(popup,"open")) return 1;
  popup->setProperty("page",1);
  auto* focus_field = window->findChild<QObject*>("sceneLensFocusField");
  if (!focus_field || !wait([&] { return focus_field->property("visible").toBool(); }) ||
      std::abs(focus_field->property("text").toString().toDouble()-expected_photo.lens.focus_distance) > 1e-4 ||
      !window->grabWindow().save(QDir(output).filePath("focus-picked-controls.png"))) return 1;
  for (const bool right_click : {false,true}) {
    if (!QMetaObject::invokeMethod(focus_button,"clicked") ||
        !wait([&] { return !popup->property("visible").toBool() && workspace->property("focusPickArmed").toBool(); })) return 1;
    if (right_click) right_click_native_test_window(window,location.toPoint());
    else escape_native_test_window(window);
    QElapsedTimer cancel_timer; cancel_timer.start();
    wait([&] { return !workspace->property("focusPickArmed").toBool() || cancel_timer.elapsed() > 3000; });
    if (workspace->property("focusPickArmed").toBool() ||
        viewport->status().value("focusPicking").toBool() ||
        *gaussian_photo_settings(viewport->status().value("photoSettings").toMap()) != expected_photo ||
        !QMetaObject::invokeMethod(popup,"open")) { std::cerr << "Focus cancel gesture failed, right-click=" << right_click << '\n'; return 1; }
  }
  focus_proof.insert("escapeAndRightClickCancel",true);
  popup->setProperty("page",3);
  const auto swaps = std::make_shared<std::atomic_int>(0);
  const auto connection = QObject::connect(window,&QQuickWindow::frameSwapped,window,[swaps] { ++*swaps; },Qt::DirectConnection);
  QVariantList edits;
  bool passed = true;
  for (int i = 0; i < 12 && passed; ++i) {
    const auto before = viewport->status().value("publishedFrames").toInt();
    QElapsedTimer timer; timer.start();
    grain->setProperty("value",0.1+static_cast<double>(i)*0.07);
    passed = QMetaObject::invokeMethod(grain,"moved") && wait([&] {
      return viewport->status().value("publishedFrames").toInt() > before && viewport->status().value("previewPhase") == "photo";
    });
    const auto published_ms = timer.elapsed();
    const auto after_publish = swaps->load(); window->update();
    passed = passed && wait([&] { window->update(); return swaps->load() >= after_publish+2; });
    const auto status = viewport->status(), metrics = status.value("metrics").toMap();
    passed = passed && !status.value("navigating").toBool() && metrics.value("resolvedSourceReused").toBool() &&
        metrics.value("rasterizedApertureSamples").toInt() == 0 && metrics.value("filmEffectsApplied").toBool();
    edits.append(QVariantMap{{"publishedMs",published_ms},{"twoSwapsMs",timer.elapsed()},{"metrics",metrics}});
  }
  passed = window->grabWindow().save(QDir(output).filePath("film-controls.png")) && passed;
  QMetaObject::invokeMethod(popup,"close");
  // Two seconds of continuous camera input: measure actual published draft
  // frames, not just timer ticks. No claim that frameSwapped is input-to-photon.
  const auto before_motion = viewport->status().value("publishedFrames").toInt();
  viewport->beginNavigation();
  QElapsedTimer motion; motion.start();
  qint64 previous = -16;
  while (motion.elapsed() < 2000) {
    if (motion.elapsed()-previous >= 16) { viewport->orbit(0.15,0.025); previous = motion.elapsed(); }
    QCoreApplication::processEvents(QEventLoop::AllEvents,2); QThread::msleep(1);
    eligible = eligible && window->isActive() && window->isExposed();
  }
  const auto draft_frames = viewport->status().value("publishedFrames").toInt()-before_motion;
  QVariantList progressive;
  int last_progressive_count = viewport->status().value("progressiveFramesPublished").toInt();
  bool progressive_capture = false;
  QElapsedTimer settle; settle.start(); viewport->endNavigation();
  passed = wait([&] {
    const auto status = viewport->status();
    const auto count = status.value("progressiveFramesPublished").toInt();
    if (count > last_progressive_count && status.value("publishedPhase") == "refining") {
      const auto metrics = status.value("metrics").toMap();
      progressive.append(QVariantMap{{"elapsedMs", settle.elapsed()}, {"metrics", metrics}});
      if (!progressive_capture) progressive_capture = window->grabWindow().save(QDir(output).filePath("progressive-after-motion.png"));
      last_progressive_count = count;
    }
    return status.value("previewPhase") == "photo";
  }) && passed;
  const auto settled_ms = settle.elapsed();
  const auto resolved = viewport->status().value("metrics").toMap();
  passed = passed && draft_frames > 0 && !resolved.value("resolvedSourceReused").toBool() &&
      !progressive.isEmpty() && progressive_capture && resolved.value("progressiveFramesProduced").toInt() == 2;
  passed = window->grabWindow().save(QDir(output).filePath("resolved-after-motion.png")) && passed;
  QVariantMap high_quality_proof;
  if (passed) {
    const auto original_photo = *gaussian_photo_settings(viewport->status().value("photoSettings").toMap());
    auto* quality = window->findChild<QObject*>("sceneLensQualityCombo");
    auto* hint = window->findChild<QObject*>("sceneLensHighQualityHint");
    if (!quality || !hint || !QMetaObject::invokeMethod(popup,"open")) return 1;
    popup->setProperty("page",2);
    if (!wait([&] { return quality->property("visible").toBool(); })) return 1;
    quality->setProperty("currentIndex",8);
    if (!QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,8)) ||
        viewport->status().value("lensSettings").toMap().value("samples").toInt() != 4096 ||
        !hint->property("visible").toBool()) return 1;
    QElapsedTimer high_timer; high_timer.start();
    wait([&] {
      const auto status = viewport->status();
      return high_timer.elapsed() > 15000 || (status.value("publishedPhase") == "refining" &&
          status.value("metrics").toMap().value("targetApertureSamples").toInt() == 4096);
    });
    const auto partial = viewport->status().value("metrics").toMap();
    if (partial.value("targetApertureSamples").toInt() != 4096 || !partial.value("progressivePreview").toBool() ||
        !window->grabWindow().save(QDir(output).filePath("high-quality-refinement.png"))) return 1;
    high_quality_proof.insert("firstObservedPrefixMs",high_timer.elapsed());
    high_quality_proof.insert("partialMetrics",partial);
    // A film edit must preserve the extended lens schema, not downgrade it.
    grain->setProperty("value",0.31);
    if (!QMetaObject::invokeMethod(grain,"moved") ||
        std::abs(viewport->status().value("photoSettings").toMap().value("film").toMap().value("grainAmount").toDouble()-0.31) > 1e-6 ||
        viewport->status().value("lensSettings").toMap().value("samples").toInt() != 4096) return 1;
    QMetaObject::invokeMethod(popup,"close");
    const int before_interrupt = viewport->status().value("publishedFrames").toInt();
    high_timer.restart(); viewport->beginNavigation(); viewport->orbit(0.25,0);
    wait([&] { return high_timer.elapsed() > 10000 ||
        (viewport->status().value("publishedFrames").toInt() > before_interrupt && viewport->status().value("publishedPhase") == "live"); });
    const auto live = viewport->status();
    if (live.value("publishedFrames").toInt() <= before_interrupt || live.value("publishedPhase") != "live" ||
        live.value("metrics").toMap().value("apertureSamples").toInt() != 1) return 1;
    high_quality_proof.insert("navigationDraftMs",high_timer.elapsed());
    high_quality_proof.insert("filmEditPreservedExtendedLens",true);
    if (!viewport->setPhotoSettings(original_photo.description()).value("ok").toBool()) return 1;
    viewport->endNavigation();
    if (!wait([&] { return viewport->status().value("previewPhase") == "photo"; })) return 1;
    high_quality_proof.insert("restoredSamples",viewport->status().value("lensSettings").toMap().value("samples"));
    high_quality_proof.insert("passed",true);
  }
  QVariantMap photo_proof;
  if (photo_edge > 0 && passed) {
    auto* export_size = window->findChild<QObject*>("sceneExportEdge");
    bool size_selected = false;
    if (export_size) for (int i = 0; i < export_size->property("count").toInt(); ++i) {
      export_size->setProperty("currentIndex", i);
      if (export_size->property("currentText").toString().toInt() == photo_edge) { size_selected = true; break; }
    }
    if (!size_selected) return 1;
    auto settings = *gaussian_photo_settings(viewport->status().value("photoSettings").toMap());
    settings.film.grain_amount = 0.15F; settings.lens.aperture_radius = focus*0.005F;
    passed = viewport->setPhotoSettings(settings.description()).value("ok").toBool() &&
        wait([&] { return viewport->status().value("previewPhase") == "photo"; });
    const auto destination = QDir(output).filePath("high-resolution-photo.png");
    QElapsedTimer timer; timer.start();
    int maximum_tiles = 0; bool progress_capture = false;
    passed = passed && viewport->exportPhoto(destination, export_size->property("currentText").toString().toInt()).value("ok").toBool() && wait([&] {
      const auto status = viewport->status();
      const int completed = status.value("exportTilesCompleted").toInt();
      maximum_tiles = std::max(maximum_tiles, completed);
      if (!progress_capture && completed > 0 && status.value("exporting").toBool())
        progress_capture = window->grabWindow().save(QDir(output).filePath("photo-export-progress.png"));
      return status.value("lastExport") == destination || !status.value("error").toString().isEmpty();
    });
    const auto milliseconds = timer.elapsed();
    const QImage exported(destination);
    if (exported.width() > 2300 && exported.height() > 4350) {
      passed = exported.copy(1848, 1848, 400, 400).save(QDir(output).filePath("tile-junction-upper-100pct.png")) && passed;
      passed = exported.copy(1848, 3896, 400, 400).save(QDir(output).filePath("tile-junction-lower-100pct.png")) && passed;
    }
    const auto metrics = viewport->status().value("lastExportRenderMetrics").toMap();
    QFile sidecar(destination+".camera.json");
    const bool receipt_read = sidecar.open(QIODevice::ReadOnly);
    const auto camera_receipt = receipt_read ? QJsonDocument::fromJson(sidecar.readAll()).toVariant().toMap() : QVariantMap{};
    passed = passed && !exported.isNull() && std::max(exported.width(), exported.height()) == photo_edge &&
        exported.colorSpace() == QColorSpace(QColorSpace::SRgb) && metrics.value("tiledPhoto").toBool() &&
        metrics.value("filmEffectsApplied").toBool() && metrics.value("apertureSamples").toInt() == 16 &&
        maximum_tiles > 0 && progress_capture && camera_receipt.value("camera").toMap().value("photo") == settings.description() &&
        !viewport->exportPhoto(destination, photo_edge).value("ok").toBool();
    photo_proof = {{"passed", passed}, {"output", destination}, {"width", exported.width()}, {"height", exported.height()},
        {"elapsedMs", milliseconds}, {"observedCompletedTiles", maximum_tiles}, {"metrics", metrics}};
  }
  QObject::disconnect(connection);
  const QVariantMap result{{"schema","framehouse-viewer-film-proof-v1"},{"eligible",eligible},{"passed",passed},
      {"cameraIndex",camera_index},{"filmEdits",edits},{"motionDurationMs",2000},{"publishedDraftFrames",draft_frames},
      {"settleMs",settled_ms},{"resolvedMetrics",resolved},{"highResolutionPhoto",photo_proof},
      {"progressivePreviews", progressive},
      {"focusPick", focus_proof},
      {"highQualityCancellation",high_quality_proof},
      {"scope","native-viewer-film-controls-and-motion-not-input-to-photon"}};
  QFile receipt(QDir(output).filePath("receipt.json"));
  if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly) || receipt.write(QJsonDocument::fromVariant(result).toJson()) < 0) return 1;
  std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return passed && eligible ? 0 : 1;
}
