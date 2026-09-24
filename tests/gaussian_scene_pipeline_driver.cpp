#include "gaussian_scene_controller.h"
#include "gaussian_capture_preparation.h"
#include "capture_camera_metadata.h"
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() < 3) return EXIT_FAILURE;
  if (args[1] == "--camera-metadata") {
    const auto result = photo_editor::desktop::read_capture_camera_metadata(args[2]);
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("available").toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  photo_editor::desktop::GaussianSceneController scene;
  scene.setCapturePreparer(photo_editor::desktop::prepare_gaussian_capture);
  QElapsedTimer elapsed;
  elapsed.start();
  int ticks = 0;
  qint64 previous = 0, maximum_gap = 0;
  QTimer heartbeat;
  heartbeat.setInterval(50);
  QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
    const auto now = elapsed.elapsed();
    maximum_gap = qMax(maximum_gap, now - previous);
    previous = now;
    ++ticks;
  });
  heartbeat.start();
  // A real-file preparation proof does not pretend that one RAW exposure is a
  // multiview capture. It exercises the production decode/render worker only.
  QFutureWatcher<QVariantMap> preparation;
  if (args[1] == "--prepare-4d") {
    if (args.size() < 4) return EXIT_FAILURE;
    QObject::connect(&preparation, &QFutureWatcher<QVariantMap>::finished, &app, [&] {
      auto result = preparation.result();
      result.insert("elapsedMs", elapsed.elapsed()); result.insert("eventLoopTicks", ticks);
      result.insert("maxEventLoopGapMs", maximum_gap);
      std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
      app.exit(result.value("ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE);
    });
    preparation.setFuture(QtConcurrent::run([args] {
      return photo_editor::desktop::prepare_gaussian_timed_capture(args[2], args[3],
          args.size() > 4 ? args[4].toInt() : 800, std::make_shared<std::atomic_bool>(false));
    }));
    return app.exec();
  }
  if (args[1] == "--prepare-only") {
    if (args.size() < 4 || !QFileInfo(args[3]).isAbsolute() || QFileInfo::exists(args[3]) ||
        !QDir().mkpath(QDir(args[3]).filePath("images"))) return EXIT_FAILURE;
    QObject::connect(&preparation, &QFutureWatcher<QVariantMap>::finished, &app, [&] {
      auto result = preparation.result();
      result.insert("elapsedMs", elapsed.elapsed());
      result.insert("eventLoopTicks", ticks);
      result.insert("maxEventLoopGapMs", maximum_gap);
      result.insert("proofScope", "real-file-canonical-preparation-only");
      std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
      app.exit(result.value("ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE);
    });
    preparation.setFuture(QtConcurrent::run([args] {
      return photo_editor::desktop::prepare_gaussian_capture({args[2]}, args[3],
          args.size() > 4 ? args[4].toInt() : 800, std::make_shared<std::atomic_bool>(false));
    }));
    return app.exec();
  }
  QString previous_state;
  QObject::connect(&scene, &photo_editor::desktop::GaussianSceneController::changed, &app, [&] {
    auto snapshot = scene.snapshot();
    const auto state = snapshot.value("state").toString();
    if (state != previous_state) { std::cerr << state.toStdString() << '\n'; previous_state = state; }
    if (!snapshot.value("busy").toBool()) {
      snapshot.remove("capture");
      snapshot.insert("elapsedMs", elapsed.elapsed());
      snapshot.insert("eventLoopTicks", ticks);
      snapshot.insert("maxEventLoopGapMs", maximum_gap);
      std::cout << QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact).constData() << '\n';
      app.exit(state == "trained" ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  });
  QTimer::singleShot(0, &app, [&] {
    const auto result = scene.startCapture(args[1], args[2], args.size() > 3 ? args[3].toInt() : 3000,
                                         args.size() > 4 ? args[4].toInt() : 800,
                                         args.size() > 5 ? args[5].toInt() : 1000000);
    if (!result.value("ok").toBool()) {
      std::cerr << QJsonDocument::fromVariant(result).toJson().constData();
      app.exit(EXIT_FAILURE);
    }
  });
  return app.exec();
}
