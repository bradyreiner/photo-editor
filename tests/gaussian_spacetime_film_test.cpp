#include "gaussian_film_export.h"
#include "gaussian_saved_views.h"
#include "gaussian_spacetime.h"
#include "gaussian_spacetime_artifact.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrent>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
bool write(const QString& path,const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.write(data)==data.size(); }
QVariantMap verify(const std::shared_ptr<const GaussianSceneData>& scene,const QString& movie,const QString& ffmpeg,const QString& probe) {
  const auto fail=[](const QString& error) { return QVariantMap{{"ok",false},{"error",error}}; };
  const auto receipt=QJsonDocument::fromJson(read(movie+".camera-path.json")).toVariant().toMap();
  const auto time=receipt.value("sceneTime").toMap();
  const auto count=receipt.value("frameCount").toInt(), fps=receipt.value("fps").toInt();
  const QSize size(receipt.value("width").toInt(),receipt.value("height").toInt());
  const qint64 frame_bytes=static_cast<qint64>(size.width())*size.height()*3;
  if(time.value("mapping")!="linear-scene-time-v1" || time.value("endpoint")!="exclusive" ||
      count<2 || count>120 || size.width()<2 || size.height()<2 || frame_bytes*count>128*1024*1024 || fps<=0)
    return fail("Invalid test film receipt.");
  GaussianCameraPath path; path.segment_seconds=receipt.value("segmentSeconds").toDouble(); path.fps=fps;
  for(const auto& value:receipt.value("keyframes").toList()) {
    const auto camera=gaussian_saved_camera(value.toMap()); if(!camera) return fail("Invalid camera."); path.cameras.push_back(*camera);
  }
  if(path.cameras.size()<2 || path.frameCount()!=count || time.value("nominalDurationSeconds").toDouble()!=path.duration()) return fail("Invalid time duration.");
  QProcess inspect; inspect.start(probe,{"-v","error","-count_frames","-show_streams","-of","json",movie});
  if(!inspect.waitForFinished(10000) || inspect.exitCode()!=0) return fail("ffprobe failed.");
  const auto stream=QJsonDocument::fromJson(inspect.readAllStandardOutput()).toVariant().toMap().value("streams").toList().value(0).toMap();
  if(stream.value("nb_read_frames").toInt()!=count || stream.value("width").toInt()!=size.width() || stream.value("height").toInt()!=size.height() ||
      stream.value("r_frame_rate")!=QString::number(fps)+"/1" || stream.value("pix_fmt")!="yuv420p" ||
      stream.value("color_transfer")!="iec61966-2-1" || stream.value("color_space")!="bt709" ||
      stream.value("color_primaries")!="bt709" || stream.value("color_range")!="tv") return fail("Encoded frame count, rate, dimensions or color tags differ.");
  QProcess decode; decode.start(ffmpeg,{"-v","error","-i",movie,"-vf","scale=in_color_matrix=bt709:in_range=tv:out_range=pc", "-f","rawvideo","-pix_fmt","rgb24","pipe:1"});
  if(!decode.waitForFinished(15000) || decode.exitCode()!=0) return fail("Decode failed.");
  const auto decoded=decode.readAllStandardOutput(); if(decoded.size()!=frame_bytes*count) return fail("Wrong decoded byte count.");
  auto renderer=make_gaussian_scene_renderer(); if(!renderer) return fail("No native renderer.");
  double minimum_psnr=100;
  double wrong_frozen_time_psnr=100;
  const auto first=time.value("startSeconds").toDouble(), last=time.value("endSeconds").toDouble();
  for(int index=0;index<count;++index) {
    // Independent explicit affine mapping, not a production time-sampling helper.
    const double seconds=first+(last-first)*(double(index)/fps)/path.duration();
    const auto frame=renderer->render(scene,path.sample(double(index)/fps).resized(size),true,seconds);
    if(!frame.error.isEmpty() || frame.image.isNull()) return fail(frame.error);
    const auto expected=frame.image.convertToFormat(QImage::Format_RGB888);
    double squared=0;
    for(int y=0;y<size.height();++y) for(int x=0;x<size.width()*3;++x) {
      const double delta=static_cast<unsigned char>(decoded[frame_bytes*index+static_cast<qint64>(y)*size.width()*3+x])-expected.constScanLine(y)[x];
      squared+=delta*delta;
    }
    const auto psnr=squared==0 ? 100 : 10*std::log10(255.0*255.0*double(frame_bytes)/squared);
    minimum_psnr=std::min(minimum_psnr,psnr);
    if(first!=last && index==count-1) {
      const auto wrong=renderer->render(scene,path.sample(double(index)/fps).resized(size),true,first);
      if(!wrong.error.isEmpty() || wrong.image.isNull()) return fail("Negative control failed to render.");
      const auto frozen=wrong.image.convertToFormat(QImage::Format_RGB888);
      double wrong_squared=0;
      for(int y=0;y<size.height();++y) for(int x=0;x<size.width()*3;++x) {
        const double delta=static_cast<unsigned char>(decoded[frame_bytes*index+static_cast<qint64>(y)*size.width()*3+x])-frozen.constScanLine(y)[x];
        wrong_squared+=delta*delta;
      }
      wrong_frozen_time_psnr=wrong_squared==0 ? 100 : 10*std::log10(255.0*255.0*double(frame_bytes)/wrong_squared);
    }
  }
  return {{"ok",minimum_psnr>=35 && (first==last || wrong_frozen_time_psnr<35)},{"frameCount",count},{"fps",fps},{"minimumDecodedRgbPsnr",minimum_psnr},
      {"wrongFrozenTimePsnr",wrong_frozen_time_psnr},{"negativeControlApplicable",first!=last},
      {"startSeconds",first},{"endSeconds",last},{"movie",movie}};
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg"), probe=QStandardPaths::findExecutable("ffprobe");
  if(ffmpeg.isEmpty() || probe.isEmpty()) return 77;
  if(argc==3) {
    const auto project=QString::fromLocal8Bit(argv[1]), movie=QString::fromLocal8Bit(argv[2]);
    const auto receipt=QJsonDocument::fromJson(read(movie+".camera-path.json")).toVariant().toMap();
    const auto loaded=load_gaussian_spacetime_artifact(QDir(project).filePath("scene/scene.safetensors"),receipt.value("artifactSha256").toString());
    if(!loaded.scene) return 1;
    const auto result=verify(loaded.scene,movie,ffmpeg,probe); std::cout<<QJsonDocument::fromVariant(result).toJson().constData();
    return result.value("ok").toBool() ? 0 : 1;
  }
  QTemporaryDir temporary;
  const auto root=argc==2 ? QString::fromLocal8Bit(argv[1]) : QFileInfo(temporary.path()).canonicalFilePath();
  if(argc>3 || (argc==2 && (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root)))) return 2;
  auto scene=std::make_shared<GaussianSceneData>(); scene->time_origin_seconds=86400; scene->local_time_range={-1,1}; scene->focus_distance=3;
  scene->temporal_sh_degree=2;
  for(int i=0;i<2;++i) {
    const double vx=i==0 ? .65 : -.45;
    const std::array<double,16> cov{.06+vx*vx*2,0,0,vx*2, 0,.04,0,0, 0,0,.05,0, vx*2,0,0,2};
    const auto point=compile_gaussian_spacetime({0,double(i)*.25,3,0},cov,.8);
    if(!point) return 1;
    scene->points.push_back(point->spatial); scene->temporal.push_back(point->temporal);
    scene->points.back().sh[static_cast<size_t>(i)*2]=1;
    GaussianTemporalAppearance appearance; appearance.sh[1]=.4F; appearance.sh[48]=.2F;
    scene->temporal_appearance.push_back(appearance);
  }
  GaussianCamera camera; camera.name="Synthetic camera"; camera.image_size={800,600}; camera.fx=camera.fy=600; camera.cx=400; camera.cy=300;
  scene->cameras={camera};
  const auto project=QDir(root).filePath("project"); if(!QDir().mkpath(QDir(project).filePath("scene"))) return 2;
  const auto artifact=write_gaussian_spacetime_artifact(*scene,QDir(project).filePath("scene/scene.safetensors"));
  if(!artifact.value("ok").toBool()) return 1;
  scene->artifact_sha256=artifact.value("artifactSha256").toString();
  if(!write(QDir(project).filePath("scene.json"),QJsonDocument::fromVariant(QVariantMap{{"schema","framehouse-gaussian-scene-v1"},
      {"mode","spacetime-4d"},{"state","artifact-ready"},{"artifactValidation",artifact}}).toJson())) return 2;
  QVariantList results; int frames=0; double minimum=100;
  GaussianCameraPath path; path.cameras={camera,camera}; path.segment_seconds=.5;
  for(int motion=0;motion<2;++motion) for(int mode=0;mode<3;++mode) {
    path.cameras[1]=camera; if(motion) path.cameras[1].position={.4F,0,0};
    path.fps=std::array<int,4>{24,25,30,60}[static_cast<size_t>(motion*3+mode)%4];
    const GaussianFilmSceneTime time=mode==0 ? GaussianFilmSceneTime{86399,86401} : mode==1 ? GaussianFilmSceneTime{86401,86399} : GaussianFilmSceneTime{86400.125,86400.125};
    const auto destination=QDir(root).filePath(QStringLiteral("motion-%1-time-%2.mp4").arg(motion).arg(mode));
    const auto progress=std::make_shared<GaussianFilmProgress>();
    const auto exported=export_gaussian_film(scene,path,destination,256,ffmpeg,progress,time);
    if(!exported.value("ok").toBool()) { std::cerr<<QJsonDocument::fromVariant(exported).toJson().constData(); return 1; }
    const auto checked=verify(scene,destination,ffmpeg,probe);
    if(!checked.value("ok").toBool() || checked.value("startSeconds").toDouble()!=time.start_seconds ||
        checked.value("endSeconds").toDouble()!=time.end_seconds || checked.value("fps").toInt()!=path.fps ||
        checked.value("frameCount").toInt()!=path.frameCount()) { std::cerr<<QJsonDocument::fromVariant(checked).toJson().constData(); return 1; }
    const auto original=read(destination);
    if(export_gaussian_film(scene,path,destination,256,ffmpeg,progress,time).value("ok").toBool() || read(destination)!=original) return 1;
    frames+=checked.value("frameCount").toInt(); minimum=std::min(minimum,checked.value("minimumDecodedRgbPsnr").toDouble()); results.append(checked);
  }
  const auto invalid_path=QDir(root).filePath("invalid.mp4"); int rejected=0;
  for(int mode=0;mode<6;++mode) {
    std::optional<GaussianFilmSceneTime> time=GaussianFilmSceneTime{86399,86401};
    if(mode==0) time.reset();
    if(mode==1) time->start_seconds=86398;
    if(mode==2) time->end_seconds=86402;
    if(mode==3) time->start_seconds=std::numeric_limits<double>::quiet_NaN();
    if(mode==4) time->end_seconds=std::numeric_limits<double>::infinity();
    auto input=std::make_shared<GaussianSceneData>(*scene); if(mode==5) input->temporal.clear();
    const auto progress=std::make_shared<GaussianFilmProgress>();
    if(export_gaussian_film(input,path,invalid_path,256,ffmpeg,progress,time).value("ok").toBool() ||
        QFileInfo::exists(invalid_path) || QFileInfo::exists(invalid_path+".camera-path.json") || progress->completed.load()!=0) return 1;
    ++rejected;
  }
  path.segment_seconds=10;
  const auto active=std::make_shared<GaussianFilmProgress>();
  auto future=QtConcurrent::run([&] { return export_gaussian_film(scene,path,invalid_path,256,ffmpeg,active,GaussianFilmSceneTime{86399,86401}); });
  QElapsedTimer elapsed; elapsed.start();
  while(!future.isFinished() && elapsed.elapsed()<10000) {
    if(active->completed.load()>0) active->canceled=true;
    app.processEvents(); QThread::msleep(1);
  }
  active->canceled=true; future.waitForFinished();
  if(!future.result().value("canceled").toBool() || active->completed.load()<1 || active->completed.load()>=path.frameCount() ||
      QFileInfo::exists(invalid_path) || QFileInfo::exists(invalid_path+".camera-path.json")) return 1;
  const QVariantMap receipt{{"ok",true},{"encodedMovies",results.size()},{"decodedFrames",frames},{"minimumDecodedRgbPsnr",minimum},
      {"invalidIntervalsRejected",rejected},{"activeCancellation",true},{"movies",results},{"scope","synthetic-time-mapped-film-not-dynamic-training"}};
  const auto bytes=QJsonDocument::fromVariant(receipt).toJson(); if(!write(QDir(root).filePath("receipt.json"),bytes)) return 2;
  std::cout<<bytes.constData(); return 0;
}
