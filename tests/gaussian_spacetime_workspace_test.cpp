#include "gaussian_scene_controller.h"
#include "gaussian_scene_viewport.h"
#include "gaussian_spacetime_artifact.h"
#include "gaussian_spacetime.h"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QStandardPaths>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
bool wait_for(const std::function<bool()>& condition) {
  QElapsedTimer timer; timer.start();
  while(!condition() && timer.elapsed()<10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents,10); QThread::msleep(1);
  }
  return condition();
}
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path,const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.write(data)==data.size(); }
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  QTemporaryDir temporary;
  const auto root=argc==2 ? QString::fromLocal8Bit(argv[1]) : QFileInfo(temporary.path()).canonicalFilePath();
  if(argc>2 || (argc==2 && (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root)))) return 2;
  const auto project=QDir(root).filePath("project");
  if(!QDir().mkpath(QDir(project).filePath("scene"))) return 2;
  auto scene=std::make_shared<GaussianSceneData>();
  scene->time_origin_seconds=86400; scene->local_time_range={-1,1}; scene->focus_distance=3;
  GaussianCamera camera; camera.name="Synthetic camera"; camera.image_size={800,600};
  camera.fx=camera.fy=600; camera.cx=400; camera.cy=300; scene->cameras.push_back(camera);
  const std::array<double,16> covariance{0.58,0,0,1, 0,0.05,0,0, 0,0,0.04,0, 1,0,0,2};
  const auto point=compile_gaussian_spacetime({0,0,3,0},covariance,0.9);
  if(!point) return 1;
  scene->points={point->spatial}; scene->temporal={point->temporal};
  scene->points[0].sh[0]=1; scene->points[0].sh[1]=0.3F; scene->points[0].sh[2]=-1;
  const auto artifact=QDir(project).filePath("scene/scene.safetensors");
  const auto saved=write_gaussian_spacetime_artifact(*scene,artifact);
  if(!saved.value("ok").toBool()) return 1;
  const QVariantMap envelope{{"schema","framehouse-gaussian-scene-v1"},{"mode","spacetime-4d"},
      {"state","artifact-ready"},{"artifactValidation",saved},{"artifactPath","/untrusted/persisted/path"}};
  const auto manifest=QDir(project).filePath("scene.json");
  const auto manifest_bytes=QJsonDocument::fromVariant(envelope).toJson();
  if(!write(manifest,manifest_bytes)) return 2;
  GaussianSceneController controller;
  std::cerr<<"verify project\n";
  controller.setSpacetimeValidator(validate_gaussian_spacetime_artifact);
  if(!controller.openProject(project).value("ok").toBool() || !controller.snapshot().value("busy").toBool() ||
      controller.openProject(project).value("ok").toBool() ||
      !wait_for([&] { return !controller.snapshot().value("busy").toBool(); }) ||
      controller.snapshot().value("state")!="artifact-ready" || controller.snapshot().value("artifactPath")!=artifact ||
      controller.snapshot().value("trainingAvailable").toBool() || read(manifest)!=manifest_bytes) return 1;
  GaussianSceneViewport viewport; viewport.setSize({800,600}); viewport.setSource(controller.snapshot());
  std::cerr<<"load viewport\n";
  if(!wait_for([&] { return viewport.status().value("frameAvailable").toBool(); }) ||
      !viewport.status().value("temporal").toBool() || viewport.status().value("timeDurationSeconds").toDouble()!=2 ||
      viewport.status().value("metrics").toMap().value("sceneSeconds").toDouble()!=86399) return 1;
  const auto start_photo=QDir(root).filePath("start.png");
  std::cerr<<"export start\n";
  if(!viewport.exportPhoto(start_photo,800).value("ok").toBool() ||
      !wait_for([&] { return viewport.status().value("lastExport")==start_photo; })) return 1;
  viewport.seekSceneTime(0.25); // Submit a preview and queue a frozen-time photo behind it.
  std::cerr<<"queue frozen photo and scrub\n";
  const auto frozen_photo=QDir(root).filePath("frozen.png");
  if(!viewport.exportPhoto(frozen_photo,800).value("ok").toBool()) return 1;
  for(int i=0;i<100;++i) viewport.seekSceneTime(double(i)*2/99);
  if(!wait_for([&] { return viewport.status().value("lastExport")==frozen_photo &&
      viewport.status().value("metrics").toMap().value("sceneSeconds").toDouble()==86401; })) {
    std::cerr<<"Queued export lost newest time preview: "<<QJsonDocument::fromVariant(viewport.status()).toJson().constData(); return 1;
  }
  const auto receipt=QJsonDocument::fromJson(read(frozen_photo+".camera.json")).toVariant().toMap();
  if(receipt.value("sceneSeconds").toDouble()!=86399.25 || receipt.value("timeUnit")!="seconds") {
    std::cerr<<"Wrong frozen receipt: "<<QJsonDocument::fromVariant(receipt).toJson().constData(); return 1;
  }
  std::cerr<<"compare pixels\n";
  auto renderer=make_gaussian_scene_renderer();
  const auto oracle=renderer->render(scene,camera,true,86399.25);
  const auto frozen=QImage(frozen_photo).convertToFormat(QImage::Format_RGBA8888);
  if(!oracle.error.isEmpty() || frozen!=oracle.image.convertToFormat(QImage::Format_RGBA8888) || QImage(start_photo)==QImage(frozen_photo)) {
    std::cerr<<"Frozen photo differs from selected-time oracle: "<<oracle.error.toStdString(); return 1;
  }
  viewport.seekSceneTime(-100);
  if(viewport.status().value("timePositionSeconds").toDouble()!=0) { std::cerr<<"Negative time not clamped"; return 1; }
  viewport.seekSceneTime(std::numeric_limits<double>::quiet_NaN());
  if(viewport.status().value("timePositionSeconds").toDouble()!=0) { std::cerr<<"NaN time changed position"; return 1; }
  viewport.playSceneTime(true);
  if(!wait_for([&] { return viewport.status().value("timePositionSeconds").toDouble()>0.05; })) { std::cerr<<"Playback did not advance"; return 1; }
  viewport.playSceneTime(false); viewport.seekSceneTime(1.99); viewport.playSceneTime(true);
  if(!wait_for([&] { return !viewport.status().value("timePlaying").toBool(); }) ||
      viewport.status().value("timePositionSeconds").toDouble()!=2 ||
      viewport.exportFilm(QDir(root).filePath("not-supported.mp4"),800).value("ok").toBool()) {
    std::cerr<<"End playback or film guard failed: "<<QJsonDocument::fromVariant(viewport.status()).toJson().constData(); return 1;
  }
  viewport.playSceneTime(true);
  if(!wait_for([&] { return viewport.status().value("timePositionSeconds").toDouble()<1; })) return 1;
  viewport.setVisible(false);
  if(viewport.status().value("timePlaying").toBool()) { std::cerr<<"Hidden playback continued"; return 1; }
  viewport.setSource({});
  if(viewport.status().value("frameAvailable").toBool() || viewport.status().value("temporal").toBool()) { std::cerr<<"Source clear retained temporal state"; return 1; }
  // An incorrect saved integrity pin cannot silently admit an otherwise valid artifact.
  auto bad=envelope; bad["artifactValidation"]=QVariantMap{{"artifactSha256",QString(64,'0')}};
  QFile invalid_manifest(manifest);
  if(!invalid_manifest.open(QIODevice::WriteOnly|QIODevice::Truncate)) return 2;
  invalid_manifest.write(QJsonDocument::fromVariant(bad).toJson()); invalid_manifest.close();
  if(!controller.openProject(project).value("ok").toBool() ||
      !wait_for([&] { return !controller.snapshot().value("busy").toBool(); }) || controller.snapshot().value("state")!="failed") {
    std::cerr<<"Bad pin admitted: "<<QJsonDocument::fromVariant(controller.snapshot()).toJson().constData(); return 1;
  }
  if(!invalid_manifest.open(QIODevice::WriteOnly|QIODevice::Truncate) || invalid_manifest.write(manifest_bytes)!=manifest_bytes.size()) return 2;
  invalid_manifest.close();
  GaussianSceneController reopened;
  reopened.setSpacetimeValidator(validate_gaussian_spacetime_artifact);
  if(!reopened.openProject(project).value("ok").toBool()) return 1;
  reopened.cancel(); // Cancellation wins even if worker delivery is already queued.
  if(!wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) || reopened.snapshot().value("state")!="canceled" ||
      read(manifest)!=manifest_bytes || !reopened.openProject(project).value("ok").toBool() ||
      !wait_for([&] { return !reopened.snapshot().value("busy").toBool(); }) || reopened.snapshot().value("state")!="artifact-ready") return 1;
  auto detached=std::make_unique<GaussianSceneViewport>(); detached->setSize({800,600}); detached->setSource(reopened.snapshot());
  if(!wait_for([&] { return detached->status().value("frameAvailable").toBool(); })) return 1;
  detached->seekSceneTime(0.75);
  const auto detached_photo=QDir(root).filePath("detached.png");
  if(!detached->exportPhoto(detached_photo,800).value("ok").toBool()) return 1;
  detached->seekSceneTime(2); detached->setSource({});
  QElapsedTimer teardown; teardown.start(); detached.reset();
  const auto teardown_us=teardown.nsecsElapsed()/1000;
  if(teardown_us>=100000 || !wait_for([&] {
    return QJsonDocument::fromJson(read(detached_photo+".camera.json")).toVariant().toMap().value("sceneSeconds").toDouble()==86399.75;
  })) return 1;
  const auto detached_oracle=renderer->render(scene,camera,true,86399.75);
  if(!detached_oracle.error.isEmpty() || QImage(detached_photo).convertToFormat(QImage::Format_RGBA8888)!=
      detached_oracle.image.convertToFormat(QImage::Format_RGBA8888)) return 1;
  bool timed_film_verified=false;
  if(!QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
    GaussianSceneViewport film_view; film_view.setSize({800,600}); film_view.setSource(reopened.snapshot());
    if(!wait_for([&] { return film_view.status().value("frameAvailable").toBool(); })) return 1;
    const auto movie=QDir(root).filePath("frozen-film-plan.mp4");
    const QVariantMap plan{{"startSeconds",.25},{"endSeconds",1.75},{"durationSeconds",.5},{"fps",24},{"cameraMotion","current-camera"}};
    for(int mode=0;mode<6;++mode) {
      auto bad_plan=plan;
      if(mode==0) bad_plan["startSeconds"]="0.25";
      if(mode==1) bad_plan["endSeconds"]=3;
      if(mode==2) bad_plan["durationSeconds"]=0;
      if(mode==3) bad_plan["fps"]=29;
      if(mode==4) bad_plan["cameraMotion"]="unknown";
      if(mode==5) bad_plan["cameraMotion"]="saved-path";
      if(film_view.exportTimedFilm(movie,bad_plan,256).value("ok").toBool() || QFileInfo::exists(movie)) return 1;
    }
    if(!film_view.exportTimedFilm(movie,plan,256).value("ok").toBool() || film_view.exportTimedFilm(movie,plan,256).value("ok").toBool()) return 1;
    film_view.seekSceneTime(2); film_view.orbit(25,10);
    if(!wait_for([&] { return !film_view.status().value("filmExporting").toBool(); })) return 1;
    const auto film_result=film_view.status().value("filmResult").toMap();
    const auto film_receipt=film_result.value("receipt").toMap();
    if(!film_result.value("ok").toBool() || film_receipt.value("sceneTime").toMap().value("startSeconds").toDouble()!=86399.25 ||
        film_receipt.value("sceneTime").toMap().value("endSeconds").toDouble()!=86400.75 ||
        film_receipt.value("keyframes").toList().value(0).toMap()!=camera.description()) return 1;
    const auto first_view=film_view.saveView("Film start"); film_view.orbit(20,0);
    const auto last_view=film_view.saveView("Film end");
    if(!first_view.value("ok").toBool() || !last_view.value("ok").toBool() ||
        !film_view.saveCameraPath({{"viewIds",QVariantList{first_view.value("id"),last_view.value("id")}},
          {"segmentSeconds",1},{"fps",24}}).value("ok").toBool()) return 1;
    auto path_plan=plan; path_plan["cameraMotion"]="saved-path";
    const auto path_movie=QDir(root).filePath("saved-path-film.mp4");
    if(!film_view.exportTimedFilm(path_movie,path_plan,256).value("ok").toBool() ||
        !wait_for([&] { return !film_view.status().value("filmExporting").toBool(); })) return 1;
    const auto path_result=film_view.status().value("filmResult").toMap();
    const auto path_receipt=path_result.value("receipt").toMap();
    if(!path_result.value("ok").toBool() || path_receipt.value("segmentSeconds").toDouble()!=.5 ||
        path_receipt.value("viewIds").toStringList()!=QStringList{first_view.value("id").toString(),last_view.value("id").toString()} ||
        film_view.status().value("cameraPath").toMap().value("segmentSeconds").toDouble()!=1) return 1;
    timed_film_verified=true;
  }
  const QVariantMap result{{"ok",true},{"projectPath",project},{"queuedPhotoTimeFrozen",true},
      {"queuedPhotoPixelsExact",true},{"scrubRequests",100},{"playPauseHideEnd",true},{"badPinRejected",true},
      {"canceledReopen",true},{"detachedPhotoTimeAndPixelsExact",true},{"teardownMicroseconds",teardown_us},
      {"timedFilmPlanFrozen",timed_film_verified},
      {"scope","synthetic-4d-workspace-not-trained-dynamic-capture"}};
  const auto output=QJsonDocument::fromVariant(result).toJson();
  if(!write(QDir(root).filePath("receipt.json"),output)) return 2;
  std::cout<<output.constData(); return 0;
}
