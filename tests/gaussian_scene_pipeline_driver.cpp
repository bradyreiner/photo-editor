#include "gaussian_scene_controller.h"
#include "gaussian_capture_preparation.h"
#include "capture_camera_metadata.h"
#include "photo_render_utils.h"
#include <QGuiApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTimer>
#include <QFutureWatcher>
#include <QCryptographicHash>
#include <QFile>
#include <QtConcurrent>
#include <iostream>
#include <algorithm>

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
  // Diagnostic replay of an already extracted, unchanged video frame set.
  // Production preparation/controller/COLMAP still run; no decoder resampling
  // or different photo-matching path is substituted for the video pipeline.
  const bool video_replay = args[1] == "--video-replay";
  const bool alignment_only = args.contains("--alignment-only");
  const bool photo_capture = !args[1].startsWith("--");
  const bool same_camera = args.contains("--same-camera-and-zoom");
  const bool strict_matching = args.contains("--strict-matching");
  if ((same_camera || strict_matching || (alignment_only && !video_replay)) && !photo_capture) return EXIT_FAILURE;
  if (photo_capture) {
    QStringList flags;
    for (const auto& argument : args.mid(6)) {
      if ((argument != "--alignment-only" && argument != "--same-camera-and-zoom" && argument != "--strict-matching") ||
          flags.contains(argument)) return EXIT_FAILURE;
      flags.append(argument);
    }
    if ((same_camera || strict_matching || alignment_only) && args.size() < 7) return EXIT_FAILURE;
  }
  QVariantMap video_receipt;
  QString replay_project;
  bool replay_sharp = false, alignment_stop_requested = false;
  if (video_replay) {
    if (args.size() != 7 && !(args.size() == 8 && args[7] == "--alignment-only")) return EXIT_FAILURE;
    QFile receipt(args[2]);
    if (!receipt.open(QIODevice::ReadOnly) || receipt.size() > 4*1024*1024) return EXIT_FAILURE;
    const auto receipt_bytes = receipt.readAll();
    video_receipt = QJsonDocument::fromJson(receipt_bytes).toVariant().toMap();
    const auto frames = video_receipt.value("frames").toStringList();
    const auto source = video_receipt.value("source").toString();
    replay_sharp = video_receipt.value("schema") == "framehouse-video-capture-v2";
    if ((!replay_sharp && video_receipt.value("schema") != "framehouse-video-capture-v1") || !video_receipt.value("ok").toBool() ||
        frames.size() < 16 || frames.size() > 1920 || frames.size() != video_receipt.value("frameCount").toInt()) return EXIT_FAILURE;
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || QFileInfo(input).isSymLink()) return EXIT_FAILURE;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input) || QString::fromLatin1(hash.result().toHex()) != video_receipt.value("sourceSha256")) return EXIT_FAILURE;
    const auto frame_root = QDir(QFileInfo(receipt).canonicalPath()).filePath("video-frames");
    const auto samples = video_receipt.value("samples").toList();
    if (replay_sharp && samples.size() != frames.size()) return EXIT_FAILURE;
    QVariantMap frame_hashes;
    for (qsizetype i = 0; i < frames.size(); ++i) {
      const auto& frame = frames[i];
      if (!QFileInfo(frame).isFile() || QFileInfo(frame).isSymLink() ||
          QFileInfo(frame).canonicalPath() != frame_root || QFileInfo(frame).canonicalFilePath() != frame) return EXIT_FAILURE;
      QFile bytes(frame); QCryptographicHash frame_hash(QCryptographicHash::Sha256);
      if (!bytes.open(QIODevice::ReadOnly) || !frame_hash.addData(&bytes)) return EXIT_FAILURE;
      const auto actual_hash = QString::fromLatin1(frame_hash.result().toHex());
      if (replay_sharp && actual_hash != samples[i].toMap().value("frameSha256")) return EXIT_FAILURE;
      frame_hashes.insert(frame,actual_hash);
    }
    replay_project = args[3];
    video_receipt.insert("replayReceiptSha256",QString::fromLatin1(QCryptographicHash::hash(receipt_bytes,QCryptographicHash::Sha256).toHex()));
    video_receipt.insert("replayReceiptPath",QFileInfo(receipt).canonicalFilePath());
    video_receipt.insert("replayFrameSha256",frame_hashes);
    scene.setVideoPreparer([video_receipt,replay_sharp](const QString& input_path,const QString&,int,int count,const QString&,const QString&,
        const std::shared_ptr<std::atomic_bool>& canceled,const auto&,bool sharp) {
      if (canceled->load() || input_path != QFileInfo(video_receipt.value("source").toString()).canonicalFilePath() ||
          sharp != replay_sharp || count != video_receipt.value("frameCount").toInt()) return QVariantMap{{"ok",false},{"error","Video replay contract changed"}};
      return video_receipt;
    });
  }
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
      QStringList sources;
      if (QFileInfo(args[2]).isDir()) {
        for (const auto& file : QDir(args[2]).entryInfoList(QDir::Files | QDir::Readable,QDir::Name))
          if (!file.isSymLink() && photo_editor::desktop::source_image_supported(file.absoluteFilePath())) sources.append(file.canonicalFilePath());
      } else sources.append(args[2]);
      if (sources.isEmpty() || sources.size() > 2000) return QVariantMap{{"ok",false},{"error","Choose 1–2000 source photographs for preparation."}};
      return photo_editor::desktop::prepare_gaussian_capture(sources, args[3],
          args.size() > 4 ? args[4].toInt() : 800, std::make_shared<std::atomic_bool>(false));
    }));
    return app.exec();
  }
  QString previous_state;
  QObject::connect(&scene, &photo_editor::desktop::GaussianSceneController::changed, &app, [&] {
    auto snapshot = scene.snapshot();
    const auto state = snapshot.value("state").toString();
    if (state != previous_state) { std::cerr << state.toStdString() << '\n'; previous_state = state; }
    if (alignment_only && !alignment_stop_requested && (state == "undistorting" || state == "training-gaussians")) {
      alignment_stop_requested = true;
      scene.cancel();
    }
    if (!snapshot.value("busy").toBool()) {
      snapshot.remove("capture");
      snapshot.insert("elapsedMs", elapsed.elapsed());
      snapshot.insert("eventLoopTicks", ticks);
      snapshot.insert("maxEventLoopGapMs", maximum_gap);
      if (alignment_only) {
        const bool collected = snapshot.contains("registeredViews");
        snapshot.insert("alignmentEvidenceCollected",collected);
        snapshot.insert("captureCoverageGatePassed",collected && snapshot.value("registeredViews").toInt() >= std::max(8,(snapshot.value("sourceCount").toInt()+1)/2));
        snapshot.insert("stoppedBeforeTraining",alignment_stop_requested || !snapshot.contains("trainingSchedule"));
        snapshot.insert("proofScope",video_replay ? "Production video preparation and COLMAP alignment only; intentional cancellation before training, not a trained-scene success" :
            "Production photo preparation and COLMAP alignment only; intentional cancellation before training, not a trained-scene success");
      }
      std::cout << QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact).constData() << '\n';
      app.exit((alignment_only ? snapshot.value("alignmentEvidenceCollected").toBool() : state == "trained") ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  });
  QTimer::singleShot(0, &app, [&] {
    const auto result = video_replay ? scene.startVideo(video_receipt.value("source").toString(),replay_project,args[4].toInt(),args[5].toInt(),args[6].toInt(),video_receipt.value("frameCount").toInt(),replay_sharp) : scene.startPhotoCapture(args[1], args[2], args.size() > 3 ? args[3].toInt() : 3000,
                                         args.size() > 4 ? args[4].toInt() : 800,
                                         args.size() > 5 ? args[5].toInt() : 1000000, same_camera, strict_matching);
    if (!result.value("ok").toBool()) {
      std::cerr << QJsonDocument::fromVariant(result).toJson().constData();
      app.exit(EXIT_FAILURE);
    }
  });
  return app.exec();
}
