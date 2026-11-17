#include "gaussian_scene_controller.h"
#include "gaussian_ply_validation.h"
#include "gaussian_photo_capture_options.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>

using photo_editor::desktop::GaussianSceneController;

static bool write_file(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray ply() {
  QByteArray bytes("ply\nformat binary_little_endian 1.0\nelement vertex 1\n");
  const QStringList properties{"x", "y", "z", "f_dc_0", "f_dc_1", "f_dc_2", "opacity", "scale_0", "scale_1", "scale_2", "rot_3", "rot_1", "rot_0", "rot_2"};
  for (const auto& property : properties) bytes += "property float " + property.toUtf8() + '\n';
  bytes += "end_header\n";
  QByteArray payload;
  QDataStream stream(&payload, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  for (const auto& property : properties) stream << (property == "rot_2" ? 1.0F : 0.0F);
  return bytes + payload;
}
static bool wait_until(const std::function<bool()>& predicate) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!predicate() && elapsed.elapsed() < 8000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  return predicate();
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  const auto option = [&](const QString& key) { return args.value(args.indexOf(key) + 1); };
  if (args.size() == 3 && args[1] == "--validate") {
    const auto result = photo_editor::desktop::validate_gaussian_ply(args[2], {});
    std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return result.value("ok").toBool() ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (args.size() > 1) {
    if (args[1] == qEnvironmentVariable("FRAMEHOUSE_TEST_FAIL_STAGE")) return 3;
    const auto photo_options = qEnvironmentVariable("FRAMEHOUSE_TEST_PHOTO_OPTIONS");
    if (!photo_options.isEmpty()) {
      if (args[1] == "feature_extractor" && photo_options.contains("shared") &&
          (option("--ImageReader.single_camera") != "1" || option("--ImageReader.camera_model") != "SIMPLE_RADIAL" ||
           args.contains("--ImageReader.camera_params") || args.contains("--ImageReader.single_camera_per_image") || args.contains("--image_list_path"))) return 1;
      if (args[1] == "exhaustive_matcher" &&
          (photo_options.contains("strict") ? option("--SiftMatching.max_ratio") != "0.65" || args.contains("--FeatureMatching.guided_matching") : args.contains("--SiftMatching.max_ratio"))) return 1;
    } else if (args.contains("--SiftMatching.max_ratio")) return 1;
    if (args[1] == "feature_extractor" && args.contains("--image_list_path")) {
      const auto list = QFileInfo(option("--image_list_path")).fileName();
      if (list == "camera-group-0000.txt") {
        if (option("--ImageReader.single_camera") != "1" || option("--ImageReader.camera_params") != "600,256,192,0") return 1;
      } else if (list == "camera-group-0001.txt") {
        if (option("--ImageReader.single_camera_per_image") != "1" || args.contains("--ImageReader.camera_params")) return 1;
      } else return 1;
      QFile stages("feature-groups.log");
      if (!stages.open(QIODevice::WriteOnly | QIODevice::Append) || stages.write(list.toUtf8() + '\n') < 0) return 1;
    }
    if (args[1] == "sequential_matcher") {
      if (option("--SequentialMatching.overlap") != "20" || option("--SequentialMatching.loop_detection") != "0" ||
          option("--FeatureMatching.guided_matching") != "1") return 1;
    }
    if (args[1] == "mapper") {
      const auto root = QDir(option("--output_path")).filePath("0");
      QByteArray count;
      QDataStream stream(&count, QIODevice::WriteOnly);
      stream.setByteOrder(QDataStream::LittleEndian);
      stream << qEnvironmentVariable("FRAMEHOUSE_TEST_REGISTERED_VIEWS","8").toULongLong();
      return write_file(QDir(root).filePath("images.bin"), count) && write_file(QDir(root).filePath("cameras.bin"), count) ? 0 : 1;
    }
    if (args.contains("--export-path")) {
      if (option("--refine-every") != "32" || option("--growth-stop-iter") != "160" ||
          option("--seed") != "42" || option("--max-splats") != qEnvironmentVariable("FRAMEHOUSE_TEST_SPLAT_BUDGET", "1000000")) return 1;
      return write_file(QDir(option("--export-path")).filePath("scene.ply"), ply()) ? 0 : 1;
    }
    return 0;
  }
  QTemporaryDir temp;
  const auto source = QDir(temp.path()).filePath("source");
  for (int i = 0; i < 8; ++i)
    if (!write_file(QDir(source).filePath(QStringLiteral("photo%1.jpg").arg(i)), "original")) return 1;
  qputenv("PHOTO_EDITOR_COLMAP_BINARY", QCoreApplication::applicationFilePath().toUtf8());
  qputenv("PHOTO_EDITOR_BRUSH_BINARY", QCoreApplication::applicationFilePath().toUtf8());
  GaussianSceneController controller;
  controller.setCapturePreparer([](const QStringList& files, const QString& project, int, const std::shared_ptr<std::atomic_bool>&) {
    if (!write_file(QDir(project).filePath("camera-group-0000.txt"), "frame_000000.png\n") ||
        !write_file(QDir(project).filePath("camera-group-0001.txt"), "frame_000001.png\n")) return QVariantMap{{"ok", false}};
    QVariantList frames;
    for (const auto& file : files) frames.append(QVariantMap{{"sourcePath",file},{"width",512},{"height",384},
        {"sourceWidth",2000},{"sourceHeight",1500},{"optics",QVariantMap{{"sourceFormat","jpeg"}}}});
    return QVariantMap{{"ok", true}, {"frameCount", files.size()}, {"frames",frames}, {"focalPriorFrames", 4}, {"estimatedIntrinsicsFrames", 4},
        {"cameraGroups", QVariantList{
            QVariantMap{{"imageListPath", "camera-group-0000.txt"}, {"prior", QVariantMap{{"available", true}, {"parameters", "600,256,192,0"}}}},
            QVariantMap{{"imageListPath", "camera-group-0001.txt"}, {"prior", QVariantMap{{"available", false}}}}}}};
  });
  if (controller.startCapture(source, source, 200, 512).value("ok").toBool()) return 1;
  const auto rejected_budget_project = QDir(temp.path()).filePath("invalid-budget");
  for (const auto budget : {0, 99999, 2000001}) {
    if (controller.startCapture(source, rejected_budget_project, 200, 512, budget).value("ok").toBool() ||
        QFileInfo::exists(rejected_budget_project)) return 1;
  }
  const auto project = QDir(temp.path()).filePath("project");
  if (!controller.startCapture(source, project, 200, 512).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state").toString() != "trained") {
    std::cerr << QJsonDocument::fromVariant(controller.snapshot()).toJson().constData();
    return 1;
  }
  QFile groups(QDir(project).filePath("feature-groups.log"));
  if (!groups.open(QIODevice::ReadOnly) || groups.readAll() != "camera-group-0000.txt\ncamera-group-0001.txt\n" ||
      controller.snapshot().value("focalPriorFrames").toInt() != 4 || controller.snapshot().value("estimatedIntrinsicsFrames").toInt() != 4)
    return 1;
  groups.close();
  const auto schedule = controller.snapshot().value("trainingSchedule").toMap();
  if (schedule.value("refineEvery").toInt() != 32 || schedule.value("settlingSteps").toInt() != 40 ||
      schedule.value("contract") != "capture-sized-refinement-v1") return 1;
  GaussianSceneController reopened;
  QFile saved_project(QDir(project).filePath("scene.json"));
  if (!saved_project.open(QIODevice::ReadOnly)) return 1;
  const auto original_project_bytes = saved_project.readAll();
  saved_project.close();
  if (!reopened.openProject(project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state").toString() != "trained" ||
      reopened.snapshot().value("trainingSchedule").toMap() != schedule) return 1;
  // The native scene menu must discover durable projects and open through the
  // same asynchronous integrity validation as the ordinary folder picker.
  const auto library_environment = qgetenv("PHOTO_EDITOR_SCENE_LIBRARY_DIR");
  qputenv("PHOTO_EDITOR_SCENE_LIBRARY_DIR", temp.path().toUtf8());
  const QVariantMap library{{"scenes", QVariantList{
      QVariantMap{{"name", "Editable scene"}, {"project", "project"}, {"viewId", "explore"}},
      QVariantMap{{"name", "Missing project"}, {"project", "missing"}},
      QVariantMap{{"name", "Outside library"}, {"project", ".."}}}}};
  if (!write_file(QDir(temp.path()).filePath("index.json"), QJsonDocument::fromVariant(library).toJson())) return 1;
  GaussianSceneController library_controller;
  const auto available_scenes = library_controller.property("exampleScenes").toList();
  if (available_scenes.size() != 1 || available_scenes.front().toMap().value("label") != "Editable scene" ||
      available_scenes.front().toMap().value("viewId") != "explore" ||
      !library_controller.openProject(available_scenes.front().toMap().value("key").toString()).value("ok").toBool() ||
      !wait_until([&] { return !library_controller.snapshot().value("busy").toBool(); }) ||
      library_controller.snapshot().value("state") != "trained") return 1;
  if (library_environment.isNull()) qunsetenv("PHOTO_EDITOR_SCENE_LIBRARY_DIR");
  else qputenv("PHOTO_EDITOR_SCENE_LIBRARY_DIR", library_environment);
  // Exercise the actual QML-visible five-argument invocation through MOC, then
  // check its child-process arguments and persisted/reopened training budget.
  const auto detail_project = QDir(temp.path()).filePath("detail-project");
  qputenv("FRAMEHOUSE_TEST_SPLAT_BUDGET", "2000000");
  QVariantMap detail_result;
  if (!QMetaObject::invokeMethod(&controller, "startCapture", Q_RETURN_ARG(QVariantMap, detail_result),
      Q_ARG(QString, source), Q_ARG(QString, detail_project), Q_ARG(int, 200), Q_ARG(int, 3200), Q_ARG(int, 2000000)) ||
      !detail_result.value("ok").toBool() || !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state") != "trained" || controller.snapshot().value("maxSplats").toInt() != 2000000 ||
      controller.snapshot().value("trainingSchedule").toMap().value("maxSplats").toInt() != 2000000 ||
      !reopened.openProject(detail_project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("maxSplats").toInt() != 2000000) return 1;
  qunsetenv("FRAMEHOUSE_TEST_SPLAT_BUDGET");
  // Native/QML invocation must freeze both choices, use estimated shared
  // intrinsics (no fabricated EXIF prior), and retain them through reopening.
  const auto photo_options_project = QDir(temp.path()).filePath("photo-options-project");
  qputenv("FRAMEHOUSE_TEST_PHOTO_OPTIONS", "shared-strict");
  QVariantMap photo_options_result;
  if (!QMetaObject::invokeMethod(&controller, "startPhotoCapture", Q_RETURN_ARG(QVariantMap, photo_options_result),
      Q_ARG(QString, source), Q_ARG(QString, photo_options_project), Q_ARG(int,200), Q_ARG(int,512), Q_ARG(int,1000000),
      Q_ARG(bool,true), Q_ARG(bool,true)) || !photo_options_result.value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state") != "trained" ||
      !controller.snapshot().value("photoSharedIntrinsics").toBool() || !controller.snapshot().value("photoStrictMatching").toBool() ||
      controller.snapshot().value("matchingStrategy") != "photo-exhaustive-strict-065" ||
      controller.snapshot().value("focalPriorFrames").toInt() != 0 || controller.snapshot().value("cameraGroupCount").toInt() != 0 ||
      controller.snapshot().value("estimatedIntrinsicsFrames").toInt() != 8 ||
      !reopened.openProject(photo_options_project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      !reopened.snapshot().value("photoSharedIntrinsics").toBool() || !reopened.snapshot().value("photoStrictMatching").toBool()) return 1;
  qunsetenv("FRAMEHOUSE_TEST_PHOTO_OPTIONS");
  const auto prepared = controller.snapshot().value("capture").toMap();
  const auto shared = photo_editor::desktop::gaussian_share_photo_intrinsics(prepared);
  if (!shared.value("ok").toBool() || shared.value("intrinsics").toMap().value("numericPriorSupplied").toBool()) return 1;
  for (const auto* field : {"width", "height", "sourceWidth", "sourceHeight", "sourceFormat", "make", "model", "bodySerial", "lens", "focalMm", "focal35Mm"}) {
    auto changed = prepared; auto frames = changed.value("frames").toList();
    // Make a known identity/focal available on one frame, then contradict it
    // on another. Missing metadata remains an explicit user assertion.
    for (int i = 0; i < 2; ++i) {
      auto frame = frames[i].toMap(); auto optics = frame.value("optics").toMap();
      const QString key = QString::fromLatin1(field);
      if (key == "width" || key == "height" || key == "sourceWidth" || key == "sourceHeight") {
        if (i == 1) frame.insert(key, frame.value(key).toInt()+1);
      } else if (key.startsWith("focal")) optics.insert(key, i == 0 ? 35.0 : 50.0);
      else optics.insert(key, i == 0 ? "first" : "second");
      frame.insert("optics", optics); frames[i] = frame;
    }
    changed.insert("frames",frames);
    if (photo_editor::desktop::gaussian_share_photo_intrinsics(changed).value("ok").toBool()) return 1;
  }
  for (const bool shared_only : {true, false}) {
    qputenv("FRAMEHOUSE_TEST_PHOTO_OPTIONS",shared_only ? "shared" : "strict");
    if (!controller.startPhotoCapture(source,QDir(temp.path()).filePath(shared_only ? "shared-only" : "strict-only"),
        200,512,1000000,shared_only,!shared_only).value("ok").toBool() ||
        !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state") != "trained" ||
        controller.snapshot().value("photoSharedIntrinsics").toBool() != shared_only ||
        controller.snapshot().value("photoStrictMatching").toBool() == shared_only) return 1;
  }
  qputenv("FRAMEHOUSE_TEST_PHOTO_OPTIONS","shared-strict");
  qputenv("FRAMEHOUSE_TEST_REGISTERED_VIEWS","4");
  if (!controller.startPhotoCapture(source,QDir(temp.path()).filePath("strict-low-coverage"),200,512,1000000,true,true).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state") != "failed" ||
      controller.snapshot().contains("trainingSchedule") ||
      !controller.snapshot().value("error").toString().contains("Reject ambiguous matches turned off")) return 1;
  qunsetenv("FRAMEHOUSE_TEST_REGISTERED_VIEWS");
  qunsetenv("FRAMEHOUSE_TEST_PHOTO_OPTIONS");
  qputenv("PHOTO_EDITOR_FFMPEG_BINARY", QCoreApplication::applicationFilePath().toUtf8());
  qputenv("PHOTO_EDITOR_FFPROBE_BINARY", QCoreApplication::applicationFilePath().toUtf8());
  const auto video = QDir(temp.path()).filePath("capture.MOV");
  if (!write_file(video, "original-video")) return 1;
  bool expected_sharp_selection = false, saw_sharp_selection = false;
  controller.setVideoPreparer([&](const QString& path, const QString&, int edge, int count, const QString&, const QString&,
      const std::shared_ptr<std::atomic_bool>&, const auto&, bool prefer_sharp_frames) {
    if (prefer_sharp_frames != expected_sharp_selection) return QVariantMap{{"ok",false},{"error","Video selection mode was not forwarded"}};
    saw_sharp_selection = prefer_sharp_frames;
    if (path != QFileInfo(video).canonicalFilePath() || edge != 512 || count != 16) return QVariantMap{{"ok", false}, {"error", "Wrong video worker arguments"}};
    QStringList frames;
    for (int i = 0; i < 8; ++i) frames.append(QDir(source).filePath(QStringLiteral("photo%1.jpg").arg(i)));
    return QVariantMap{{"ok", true}, {"frames", frames}, {"frameCount", 8}, {"sourceSha256", "fixture-only"},{"preferSharpFrames",prefer_sharp_frames}};
  });
  const auto video_project = QDir(temp.path()).filePath("video-project");
  if (controller.startVideo(video, video_project, 200, 512, 1000000, 0).value("ok").toBool() || QFileInfo::exists(video_project)) return 1;
  if (controller.startVideo(video, video_project, 200, 512, 1000000, 1921).value("ok").toBool() || QFileInfo::exists(video_project)) return 1;
  QVariantMap video_result;
  if (!QMetaObject::invokeMethod(&controller, "startVideo", Q_RETURN_ARG(QVariantMap, video_result),
      Q_ARG(QString, video), Q_ARG(QString, video_project), Q_ARG(int, 200), Q_ARG(int, 512), Q_ARG(int, 1000000), Q_ARG(int, 16)) ||
      !video_result.value("ok").toBool() || !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state") != "trained" || controller.snapshot().value("sourceCount").toInt() != 8 ||
      controller.snapshot().value("sourceVideo") != QFileInfo(video).canonicalFilePath() ||
      controller.snapshot().value("matchingStrategy") != "video-sequential-guided-20" ||
      !reopened.openProject(video_project).value("ok").toBool() || !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("video").toMap().value("sourceSha256") != "fixture-only") {
    std::cerr << "Video controller handoff failed: " << QJsonDocument::fromVariant(video_result).toJson().constData()
        << QJsonDocument::fromVariant(controller.snapshot()).toJson().constData()
        << QJsonDocument::fromVariant(reopened.snapshot()).toJson().constData(); return 1;
  }
  expected_sharp_selection = true;
  const auto sharp_project = QDir(temp.path()).filePath("sharp-video-project");
  if (!QMetaObject::invokeMethod(&controller,"startVideo",Q_RETURN_ARG(QVariantMap,video_result),
      Q_ARG(QString,video),Q_ARG(QString,sharp_project),Q_ARG(int,200),Q_ARG(int,512),Q_ARG(int,1000000),Q_ARG(int,16),Q_ARG(bool,true)) ||
      !video_result.value("ok").toBool() || !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      !saw_sharp_selection || controller.snapshot().value("state") != "trained" ||
      !controller.snapshot().value("preferSharpFrames").toBool() ||
      !reopened.openProject(sharp_project).value("ok").toBool() || !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      !reopened.snapshot().value("video").toMap().value("preferSharpFrames").toBool()) return 1;
  qputenv("FRAMEHOUSE_TEST_REGISTERED_VIEWS","3");
  const auto poor_sharp_project = QDir(temp.path()).filePath("poor-sharp-video-project");
  if (!controller.startVideo(video,poor_sharp_project,200,512,1000000,16,true).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state") != "failed" || controller.snapshot().value("registeredViews").toInt() != 3 ||
      !controller.snapshot().value("error").toString().contains("uniform sampling") ||
      controller.snapshot().contains("trainingSchedule")) return 1;
  qunsetenv("FRAMEHOUSE_TEST_REGISTERED_VIEWS");
  // Explicit partial recovery preserves the failed receipt and does not rerun
  // extraction/matching. Opening alone must remain read-only and idle.
  const auto partial_project = QDir(temp.path()).filePath("partial-project");
  QDir().mkpath(QDir(partial_project).filePath("images"));
  QDir().mkpath(QDir(partial_project).filePath("trained"));
  QDir().mkpath(QDir(partial_project).filePath("sparse/0"));
  if (!QFile::copy(QDir(video_project).filePath("sparse/0/images.bin"),
      QDir(partial_project).filePath("sparse/0/images.bin")) ||
      !QFile::copy(QDir(video_project).filePath("sparse/0/cameras.bin"), QDir(partial_project).filePath("sparse/0/cameras.bin"))) return 1;
  auto partial_state = controller.snapshot();
  partial_state.insert("state", "failed"); partial_state.insert("stageIndex", 2);
  partial_state.insert("sourceCount", 17); partial_state.insert("registeredViews", 8);
  partial_state.insert("error", "Insufficient connected coverage");
  QStringList partial_frames;
  for (int i = 0; i < 17; ++i) partial_frames.append(QStringLiteral("fixture-%1.png").arg(i));
  partial_state.insert("video", QVariantMap{{"frames", partial_frames}});
  const auto partial_bytes = QJsonDocument::fromVariant(partial_state).toJson();
  if (!write_file(QDir(partial_project).filePath("scene.json"), partial_bytes) ||
      !reopened.openProject(partial_project).value("ok").toBool() || reopened.snapshot().value("busy").toBool() ||
      QFileInfo::exists(QDir(partial_project).filePath("scene-before-partial.json"))) return 1;
  qputenv("FRAMEHOUSE_TEST_FAIL_STAGE", "feature_extractor");
  if (!reopened.buildPartialScene().value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state") != "trained" || !reopened.snapshot().value("partialScene").toBool() ||
      reopened.snapshot().value("registeredViews").toInt() != 8 || reopened.snapshot().value("sourceCount").toInt() != 17 ||
      reopened.buildPartialScene().value("ok").toBool()) {
    std::cerr << QJsonDocument::fromVariant(reopened.snapshot()).toJson().constData(); return 1;
  }
  qunsetenv("FRAMEHOUSE_TEST_FAIL_STAGE");
  QFile prior_receipt(QDir(partial_project).filePath("scene-before-partial.json"));
  if (!prior_receipt.open(QIODevice::ReadOnly) || prior_receipt.readAll() != partial_bytes ||
      !reopened.openProject(partial_project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      !reopened.snapshot().value("partialScene").toBool()) return 1;
  qunsetenv("PHOTO_EDITOR_FFMPEG_BINARY"); qunsetenv("PHOTO_EDITOR_FFPROBE_BINARY");
  const auto artifact = QDir(project).filePath("trained/scene.ply");
  if (!photo_editor::desktop::validate_gaussian_ply(artifact, {}).value("ok").toBool()) return 1;
  auto changed_artifact = ply();
  const auto payload_start = changed_artifact.indexOf("end_header\n") + 11;
  qToLittleEndian<quint32>(std::bit_cast<quint32>(1.0F), changed_artifact.data() + payload_start);
  if (!write_file(artifact, changed_artifact) || !reopened.openProject(project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state").toString() != "failed") return 1;
  if (!saved_project.open(QIODevice::ReadOnly) || saved_project.readAll() != original_project_bytes) return 1;
  saved_project.close();
  auto invalid = ply();
  invalid.chop(4);
  if (!write_file(artifact, invalid) || photo_editor::desktop::validate_gaussian_ply(artifact, {}).value("ok").toBool()) return 1;
  invalid = ply();
  const auto data_start = invalid.indexOf("end_header\n") + 11;
  qToLittleEndian<quint32>(0x7fc00000U, invalid.data() + data_start);
  if (!write_file(artifact, invalid) || photo_editor::desktop::validate_gaussian_ply(artifact, {}).value("ok").toBool()) return 1;
  qputenv("FRAMEHOUSE_TEST_FAIL_STAGE", "feature_extractor");
  if (!controller.startCapture(source, QDir(temp.path()).filePath("failed-project"), 200, 512).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state").toString() != "failed") return 1;
  qunsetenv("FRAMEHOUSE_TEST_FAIL_STAGE");
  controller.setCapturePreparer([](const QStringList&, const QString& root, int, const std::shared_ptr<std::atomic_bool>&) {
    const auto receipt = QDir(root).filePath("scene.json");
    // Deliberately make the owned test receipt unwritable as a regular file.
    if (!QFile::remove(receipt) || !QDir().mkdir(receipt)) return QVariantMap{{"ok", false}};
    return QVariantMap{{"ok", true}};
  });
  if (!controller.startCapture(source, QDir(temp.path()).filePath("unsavable-project"), 200, 512).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state").toString() != "failed" ||
      controller.snapshot().value("persistenceError").toString().isEmpty()) return 1;
  controller.setCapturePreparer([](const QStringList&, const QString&, int, const std::shared_ptr<std::atomic_bool>& canceled) {
    while (!canceled->load()) QThread::msleep(5);
    return QVariantMap{{"ok", false}, {"error", "canceled"}};
  });
  if (!controller.startCapture(source, QDir(temp.path()).filePath("canceled-project"), 200, 512).value("ok").toBool()) return 1;
  controller.cancel();
  if (!wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state").toString() != "canceled") return 1;
  QFile original(QDir(source).filePath("photo0.jpg"));
  if (!original.open(QIODevice::ReadOnly) || original.readAll() != "original") return 1;
  const auto imported_source = QDir(temp.path()).filePath("external.ply");
  const auto imported_project = QDir(temp.path()).filePath("imported");
  if (!write_file(imported_source, ply()) || !controller.importPly(imported_source, imported_project).value("ok").toBool() ||
      !wait_until([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state") != "trained" || !controller.snapshot().value("importedArtifact").toBool() ||
      controller.importPly(imported_source, imported_project).value("ok").toBool() ||
      !reopened.openProject(imported_project).value("ok").toBool() ||
      !wait_until([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state") != "trained" || !reopened.snapshot().value("importedArtifact").toBool()) return 1;
  std::cout << "scene protocol, artifact validation, failure, cancellation, reopen and original preservation pass\n";
  return 0;
}
