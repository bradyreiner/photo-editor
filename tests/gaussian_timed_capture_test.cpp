#include "gaussian_capture_preparation.h"
#include "gaussian_timed_capture_manifest.h"
#include "gaussian_scene_controller.h"
#include "photo_render_utils.h"
#include <QColorSpace>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <iostream>

namespace {
bool write_json(const QString& path, const QVariantMap& value) {
  QFile file(path); const auto bytes = QJsonDocument::fromVariant(value).toJson();
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QVariantMap read_json(const QString& path) {
  QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
}
QString sha256(const QString& path) {
  QFile file(path); QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return QString::fromLatin1(hash.result().toHex());
}
bool wait_for(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(2); }
  return predicate();
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 1 && args.size() != 3) return 2;
  QTemporaryDir temporary;
  const QString root = args.size() == 3 ? args[2] : temporary.path();
  if (args.size() == 3 && (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root))) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", QDir(root).filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(root).filePath("cache").toUtf8());
  const auto capture_root = QDir(root).filePath("capture");
  if (!QDir().mkpath(capture_root)) return 2;
  const auto size = args.size() == 3 ? photo_editor::desktop::source_image_size(args[1]) : QSize(512, 384);
  if (size.isEmpty()) return 2;
  QVariantList cameras, frames;
  for (int camera = 0; camera < 2; ++camera) {
    QVariantList matrix{1,0,0,camera, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    cameras.append(QVariantMap{{"id", QString::number(camera)}, {"model", "PINHOLE"}, {"undistorted", true},
        {"width", size.width()}, {"height", size.height()}, {"fx", 400.0}, {"fy", 410.0},
        {"cx", size.width()/2.0}, {"cy", size.height()/2.0}, {"worldToCamera", matrix}});
  }
  for (int time = 0; time < 2; ++time) {
    QVariantList images;
    for (int camera = 0; camera < 2; ++camera) {
      const auto name = QStringLiteral("view-%1-%2.%3").arg(time).arg(camera)
          .arg(args.size() == 3 ? QFileInfo(args[1]).suffix() : "png");
      const auto path = QDir(capture_root).filePath(name);
      if (args.size() == 3) { if (!QFile::copy(args[1], path)) return 2; }
      else {
        QImage image(size, QImage::Format_ARGB32); image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        for (int y=0;y<size.height();++y) for (int x=0;x<size.width();++x)
          image.setPixelColor(x,y,QColor((x+time*20)%220,(y+camera*30)%220,(x+y)%220));
        if (!image.save(path)) return 2;
      }
      images.append(QVariantMap{{"cameraId", QString::number(camera)}, {"file", name}});
    }
    frames.append(QVariantMap{{"timeSeconds", 2.0+time*0.05}, {"images", images}});
  }
  const QVariantMap manifest{{"schema", "framehouse-timed-capture-v1"}, {"timeUnit", "seconds"},
      {"worldUnit", "metres"}, {"cameraConvention", "opencv-world-to-camera"}, {"pixelConvention", "top-left-corner"},
      {"cameras", cameras}, {"frames", frames}};
  const auto manifest_path = QDir(capture_root).filePath("capture-4d.json");
  if (!write_json(manifest_path, manifest)) return 2;
  using namespace photo_editor::desktop;
  const auto valid = read_gaussian_timed_capture(manifest_path);
  if (!valid.valid() || valid.sources.size()!=4 || valid.time_count!=2) { std::cerr<<valid.error.toStdString(); return 1; }
  int rejected = 0;
  const auto reject = [&](QVariantMap value) {
    if (!write_json(manifest_path, value)) return false;
    const auto invalid = read_gaussian_timed_capture(manifest_path);
    if (invalid.valid()) return false;
    ++rejected; return true;
  };
  for (const auto& key : {"schema", "timeUnit", "worldUnit", "cameraConvention", "pixelConvention"}) {
    auto bad=manifest; bad[key]="invalid"; if (!reject(bad)) return 1;
  }
  for (const auto& key : {"fx", "width", "undistorted"}) {
    auto bad=manifest; auto list=cameras; auto camera=list[0].toMap(); camera[key]=key==QString("undistorted") ? QVariant(false) : QVariant("400");
    list[0]=camera; bad["cameras"]=list; if (!reject(bad)) return 1;
  }
  for (int mode=0;mode<5;++mode) {
    auto bad=manifest; auto list=cameras; auto camera=list[0].toMap(); auto matrix=camera["worldToCamera"].toList();
    if(mode==0) matrix[0]=2; if(mode==1) matrix[0]=-1; if(mode==2) matrix[15]=0;
    if(mode==3) matrix.removeLast(); if(mode==4) camera["id"]="1";
    camera["worldToCamera"]=matrix; list[0]=camera; bad["cameras"]=list; if(!reject(bad)) return 1;
  }
  for(int mode=0;mode<7;++mode) {
    auto bad=manifest; auto list=frames; auto frame=list[1].toMap(); auto images=frame["images"].toList(); auto observation=images[0].toMap();
    if(mode==0) frame["timeSeconds"]=2.0;
    if(mode==1) frame["timeSeconds"]=-1.0;
    if(mode==2) observation["file"]="../outside.png";
    if(mode==3) observation["file"]="/tmp/outside.png";
    if(mode==4) observation["cameraId"]="unknown";
    if(mode==5) observation["file"]=frames[0].toMap()["images"].toList()[0].toMap()["file"];
    images[0]=observation; if(mode==6) images.removeLast(); frame["images"]=images; list[1]=frame; bad["frames"]=list;
    if(!reject(bad)) return 1;
  }
  if (!write_json(manifest_path, manifest)) return 2;
  auto canceled=std::make_shared<std::atomic_bool>(true);
  const auto canceled_target=QDir(root).filePath("canceled");
  if(prepare_gaussian_timed_capture(manifest_path,canceled_target,256,canceled).value("ok").toBool() || QFileInfo::exists(canceled_target)) return 1;
  auto dimension_bad=manifest; auto wrong_cameras=cameras; auto camera=wrong_cameras[0].toMap(); camera["width"]=size.width()+1;
  wrong_cameras[0]=camera; dimension_bad["cameras"]=wrong_cameras; if(!write_json(manifest_path,dimension_bad)) return 2;
  canceled->store(false);
  const auto wrong_target=QDir(root).filePath("wrong-size");
  if(prepare_gaussian_timed_capture(manifest_path,wrong_target,256,canceled).value("ok").toBool() || QFileInfo::exists(wrong_target)) return 1;
  if(!write_json(manifest_path,manifest)) return 2;
  GaussianSceneController controller;
  controller.setCapturePreparer(prepare_gaussian_capture);
  const auto static_rejection=controller.startCapture(capture_root,QDir(root).filePath("static-project"));
  if(static_rejection.value("ok").toBool() || !static_rejection.value("error").toString().contains("timed capture") ||
      QFileInfo::exists(QDir(root).filePath("static-project"))) return 1;
  const auto project=QDir(root).filePath("prepared");
  auto result=prepare_gaussian_timed_capture(manifest_path,project,256,canceled);
  if(!result.value("ok").toBool()) { std::cerr<<result.value("error").toString().toStdString(); return 1; }
  const auto dataset=read_json(QDir(project).filePath("timed-capture.json"));
  const auto observations=dataset.value("observations").toList();
  if(observations.size()!=4 || result.value("trainingAvailable").toBool() || result.value("state")!="prepared-not-trained") return 1;
  for(int i=0;i<4;++i) {
    const auto observation=observations[i].toMap();
    if(observation.value("cameraId")!=valid.observations[i].toMap().value("cameraId") ||
        observation.value("timeSeconds")!=valid.observations[i].toMap().value("timeSeconds")) return 1;
    const QImage image(QDir(project).filePath(observation.value("file").toString()));
    if(image.isNull() || image.colorSpace()!=QColorSpace(QColorSpace::SRgb) ||
        observation.value("sourceSha256").toString()!=sha256(valid.sources[i]) ||
        observation.value("preparedSha256").toString()!=sha256(QDir(project).filePath(observation.value("file").toString()))) return 1;
  }
  const auto output_camera=dataset.value("cameras").toList()[0].toMap();
  if(std::abs(output_camera.value("fx").toDouble()-400.0*output_camera.value("width").toDouble()/size.width())>1e-9 ||
      output_camera.value("worldToCamera")!=cameras[0].toMap().value("worldToCamera")) return 1;
  if(prepare_gaussian_timed_capture(manifest_path,project,256,canceled).value("ok").toBool()) return 1;
  const auto validator = validate_prepared_gaussian_timed_capture;
  const auto initial_verification=validator(project,canceled);
  if (!initial_verification.value("imagesVerified").toBool()) { std::cerr << QJsonDocument::fromVariant(initial_verification).toJson().constData(); return 1; }
  controller.setTimedCaptureHandlers(prepare_gaussian_timed_capture,validator);
  const auto gui_project=QDir(root).filePath("controller-project");
  if (!controller.startTimedCapture(manifest_path,gui_project,256).value("ok").toBool() ||
      !controller.snapshot().value("busy").toBool() ||
      controller.startTimedCapture(manifest_path,QDir(root).filePath("overlap"),256).value("ok").toBool() ||
      controller.openProject(project).value("ok").toBool() ||
      !wait_for([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state")!="prepared-not-trained") { std::cerr << "controller prepare failed"; return 1; }
  const auto saved_path=QDir(gui_project).filePath("scene.json");
  const auto saved_hash=sha256(saved_path);
  GaussianSceneController reopened;
  reopened.setTimedCaptureHandlers(prepare_gaussian_timed_capture,validator);
  if (!reopened.openProject(gui_project).value("ok").toBool() || !reopened.snapshot().value("busy").toBool() ||
      !wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state")!="prepared-not-trained" || sha256(saved_path)!=saved_hash) return 1;
  if (!reopened.openProject(project).value("ok").toBool() ||
      !wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state")!="prepared-not-trained" || QFileInfo::exists(QDir(project).filePath("scene.json"))) return 1;
  const auto gui_manifest_path=QDir(gui_project).filePath("timed-capture.json");
  const auto gui_dataset=read_json(gui_manifest_path);
  auto modified=gui_dataset; modified["sourceManifestSha256"]=QString(64,'a');
  if (!write_json(gui_manifest_path,modified) || !reopened.openProject(gui_project).value("ok").toBool() ||
      !wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state")!="failed" || !reopened.snapshot().value("error").toString().contains("manifest has changed") ||
      sha256(saved_path)!=saved_hash || !write_json(gui_manifest_path,gui_dataset)) return 1;
  const auto first_path=QDir(gui_project).filePath(gui_dataset.value("observations").toList()[0].toMap().value("file").toString());
  QFile first_image(first_path);
  if (!first_image.open(QIODevice::ReadOnly)) return 2;
  const auto original_bytes=first_image.readAll(); first_image.close();
  if (!first_image.open(QIODevice::WriteOnly | QIODevice::Append) || first_image.write("changed")!=7) return 2;
  first_image.close();
  if (validator(gui_project,canceled).value("ok").toBool() || !reopened.openProject(gui_project).value("ok").toBool() ||
      !wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) ||
      reopened.snapshot().value("state")!="failed" || sha256(saved_path)!=saved_hash) return 1;
  if (!first_image.open(QIODevice::WriteOnly | QIODevice::Truncate) || first_image.write(original_bytes)!=original_bytes.size()) return 2;
  first_image.close();
  for (int mode=0;mode<5;++mode) {
    auto bad=gui_dataset; auto list=bad.value("observations").toList(); auto observation=list[1].toMap();
    if (mode==0) observation["timeIndex"]="0";
    if (mode==1) observation["timeIndex"]=2;
    if (mode==2) observation["timeSeconds"]=5;
    if (mode==3) observation["file"]="../outside.png";
    if (mode==4) bad["timeCount"]=20;
    list[1]=observation; bad["observations"]=list;
    if (!write_json(gui_manifest_path,bad) || validator(gui_project,canceled).value("ok").toBool()) return 1;
  }
  if (!write_json(gui_manifest_path,gui_dataset)) return 2;
  // Recomputed hashes must not make a wrong-size or wrong-profile image valid.
  for (int mode=0;mode<3;++mode) {
    QImage replacement(mode==0 ? QSize(16,16) : QImage::fromData(original_bytes).size(),QImage::Format_RGB32);
    replacement.fill(Qt::red);
    replacement.setColorSpace(QColorSpace(mode==0 ? QColorSpace::SRgb : QColorSpace::DisplayP3));
    if (!replacement.save(first_path,"PNG")) return 2;
    if (mode==2) {
      if (!first_image.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
          first_image.write(original_bytes.first(original_bytes.size()/2))!=original_bytes.size()/2) return 2;
      first_image.close();
    }
    auto bad=gui_dataset; auto list=bad.value("observations").toList(); auto observation=list[0].toMap();
    observation["preparedSha256"]=sha256(first_path); list[0]=observation; bad["observations"]=list;
    if (!write_json(gui_manifest_path,bad) || validator(gui_project,canceled).value("ok").toBool()) return 1;
  }
  if (!first_image.open(QIODevice::WriteOnly | QIODevice::Truncate) || first_image.write(original_bytes)!=original_bytes.size()) return 2;
  first_image.close();
  if (!write_json(gui_manifest_path,gui_dataset)) return 2;
  // Hold the shared pool: admission, cancel and destruction must not wait for
  // decoding or verification, and canceled queued work must create no output.
  auto* pool=QThreadPool::globalInstance(); pool->waitForDone();
  const auto previous_threads=pool->maxThreadCount(); pool->setMaxThreadCount(1);
  auto release=std::make_shared<std::atomic_bool>(false);
  auto blocker=QtConcurrent::run([release] { while (!release->load()) QThread::msleep(1); });
  const auto canceled_project=QDir(root).filePath("controller-canceled");
  auto pending=std::make_unique<GaussianSceneController>();
  pending->setTimedCaptureHandlers(prepare_gaussian_timed_capture,validator);
  const bool accepted=pending->startTimedCapture(manifest_path,canceled_project,256).value("ok").toBool();
  pending->cancel();
  QElapsedTimer teardown; teardown.start(); pending.reset(); const auto teardown_ms=teardown.elapsed();
  release->store(true); blocker.waitForFinished(); pool->waitForDone(); pool->setMaxThreadCount(previous_threads);
  if (!accepted || teardown_ms>100 || QFileInfo::exists(canceled_project)) return 1;
  result["controllerPrepareReopenIntegrityAndCancel"]=true;
  result["preparedMalformedCasesRejected"]=8;
  result["rejectedInvalidManifests"]=rejected; result["proofScope"]=args.size()==3 ? "single-RAW-canonical-decode-with-synthetic-timing-and-calibration" : "synthetic-timed-input-contract";
  if(!write_json(QDir(root).filePath("receipt.json"),result)) return 2;
  std::cout<<QJsonDocument::fromVariant(result).toJson().constData();
  return 0;
}
