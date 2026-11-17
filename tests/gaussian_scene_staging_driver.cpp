#include "gaussian_scene_renderer.h"
#include "gaussian_ply_validation.h"
#include "gaussian_focus_pick.h"
#include "gaussian_saved_views.h"
#include "gaussian_film_export.h"
#include "gaussian_scene_import.h"
#include "gaussian_physical_camera.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QImageReader>
#include <iostream>
#include <thread>
#include <chrono>

using namespace photo_editor::desktop;
namespace {
bool publish(const QString& path,const QByteArray& bytes) {
  QFile f(path); return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.write(bytes)==bytes.size();
}
struct Patch { QVector3D position,normal; float depth = 0; };
std::optional<Patch> patch(const GaussianSceneData& scene,const GaussianCamera& camera,float u,float v) {
  const auto point = [&](float x,float y) -> std::optional<QVector3D> {
    const QPointF pixel(std::floor(x*camera.image_size.width())+0.5,std::floor(y*camera.image_size.height())+0.5);
    const auto hit = pick_gaussian_focus(scene,camera,pixel,{},{});
    if (!hit.value("ok").toBool()) return {};
    const float z = hit.value("surfaceDepth").toFloat();
    return camera.position+z*(camera.forward+camera.right*static_cast<float>((pixel.x()-camera.cx)/camera.fx)+camera.down*static_cast<float>((pixel.y()-camera.cy)/camera.fy));
  };
  const auto c = point(u,v),l = point(u-0.015F,v),r = point(u+0.015F,v),a = point(u,v-0.015F),b = point(u,v+0.015F);
  if (!c || !l || !r || !a || !b) return {};
  auto normal = QVector3D::crossProduct(*r-*l,*b-*a).normalized();
  if (normal.lengthSquared()<0.9F) return {};
  if (QVector3D::dotProduct(normal,camera.position-*c)<0) normal = -normal;
  return Patch{*c,normal,QVector3D::dotProduct(*c-camera.position,camera.forward)};
}
QByteArray torus() {
  QByteArray bytes("# Authored staging torus; major radius 0.7, minor radius 0.22\n");
  constexpr int around = 64, tube = 24; constexpr double pi = 3.141592653589793;
  for (int i=0;i<around;++i) for (int j=0;j<tube;++j) {
    const double a=2*pi*i/around,b=2*pi*j/tube,r=0.7+0.22*std::cos(b);
    bytes += "v "+QByteArray::number(r*std::cos(a),'g',12)+' '+QByteArray::number(0.22*std::sin(b),'g',12)+' '+QByteArray::number(r*std::sin(a),'g',12)+'\n';
  }
  for (int i=0;i<around;++i) for (int j=0;j<tube;++j) {
    const double a=2*pi*i/around,b=2*pi*j/tube;
    bytes += "vn "+QByteArray::number(std::cos(b)*std::cos(a),'g',12)+' '+QByteArray::number(std::sin(b),'g',12)+' '+QByteArray::number(std::cos(b)*std::sin(a),'g',12)+'\n';
  }
  const auto index = [](int i,int j) { return (i%around)*tube+(j%tube)+1; };
  for (int i=0;i<around;++i) for (int j=0;j<tube;++j) {
    const int ids[]{index(i,j),index(i,j+1),index(i+1,j+1),index(i+1,j)};
    bytes += "f"; for (const auto id:ids) bytes += ' '+QByteArray::number(id)+"//"+QByteArray::number(id); bytes += '\n';
  }
  return bytes;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); const auto args = app.arguments();
  if (args.value(1)=="--validate-project") {
    if (args.size()!=3) return 2;
    const auto project=args[2]; QFile manifest(QDir(project).filePath("scene.json"));
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    const auto metadata=QJsonDocument::fromJson(manifest.readAll()).toVariant().toMap();
    const auto hash=metadata.value("artifactValidation").toMap().value("artifactSha256").toString();
    const auto loaded=load_gaussian_scene(QDir(project).filePath("trained/scene.ply"),
        metadata.value("importedArtifact").toBool() ? QString() : QDir(project).filePath("dataset/sparse"),hash);
    if (!loaded.scene) { std::cerr<<loaded.error.toStdString()<<'\n'; return 1; }
    const auto views=load_gaussian_saved_views(project,*loaded.scene);
    const auto composition=load_gaussian_composition(project,hash);
    if (!views.value("ok").toBool() || !composition.composition) {
      std::cerr<<views.value("error").toString().toStdString()<<composition.error.toStdString()<<'\n'; return 1;
    }
    std::cout<<QJsonDocument::fromVariant(QVariantMap{{"ok",true},{"project",project},{"artifactSha256",hash},
        {"pointCount",static_cast<qulonglong>(loaded.scene->points.size())},{"savedViews",views.value("views")},
        {"cameraPath",views.value("cameraPath")},{"composition",composition.composition->description()},
        {"scope","Native artifact hash, saved physical cameras, path compatibility and composition reload; no render or performance claim"}}).toJson().constData();
    return 0;
  }
  if (args.value(1)=="--patch-stats") {
    if (args.size()!=7 && args.size()!=8) return 2;
    QImageReader reader(args[2]); const auto size=reader.size();
    if (size.isEmpty() || size.width()>8192 || size.height()>8192) return 2;
    const auto image=reader.read();
    const QRect region(args[3].toInt(),args[4].toInt(),args[5].toInt(),args[6].toInt());
    if (image.isNull() || image.depth()!=32 || region.isEmpty() || !image.rect().contains(region)) return 2;
    std::array<double,3> sum{},second{},linear_sum{},linear_second{};
    const double count=static_cast<double>(region.width())*region.height();
    for (int y=region.top();y<=region.bottom();++y) for (int x=region.left();x<=region.right();++x) {
      const auto pixel=image.pixelColor(x,y); const std::array<int,3> rgb{pixel.red(),pixel.green(),pixel.blue()};
      for (size_t channel=0;channel<3;++channel) {
        const double code=rgb[channel],v=code/255.0;
        const double linear=v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4);
        sum[channel]+=code/count; second[channel]+=code*code/count;
        linear_sum[channel]+=linear/count; linear_second[channel]+=linear*linear/count;
      }
    }
    QVariantList channels;
    for (size_t channel=0;channel<3;++channel) {
      const double stddev=std::sqrt(std::max(0.0,second[channel]-sum[channel]*sum[channel]));
      const double linear_stddev=std::sqrt(std::max(0.0,linear_second[channel]-linear_sum[channel]*linear_sum[channel]));
      channels.append(QVariantMap{{"meanCode",sum[channel]},{"standardDeviationCode",stddev},
          {"meanLinearSrgb",linear_sum[channel]},{"standardDeviationLinearSrgb",linear_stddev},
          {"coefficientOfVariationLinearSrgb",linear_sum[channel]>0 ? linear_stddev/linear_sum[channel] : 0}});
    }
    if (args.size()==8 && (QFileInfo::exists(args[7]) || !image.copy(region).save(args[7]))) return 2;
    std::cout<<QJsonDocument::fromVariant(QVariantMap{{"ok",true},{"source",args[2]},
        {"rect",QVariantList{region.x(),region.y(),region.width(),region.height()}},{"channelsRgb",channels},
        {"resampled",false},{"scope","Stored RGB patch statistics; inverse sRGB transfer only, not a calibrated sensor or film measurement"}}).toJson().constData(); return 0;
  }
  if (args.value(1)=="--photo-study") {
    if (args.size()!=5) return 2;
    const auto source=args[2],root=args[4]; QFile input(args[3]);
    if (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !input.open(QIODevice::ReadOnly) || input.size()>2*1024*1024) return 2;
    const auto bytes=input.readAll(); const auto plan=QJsonDocument::fromJson(bytes).toVariant().toMap();
    const auto shots=plan.value("shots").toList();
    if (plan.value("schema")!="looksy-native-photo-study-v1" || shots.isEmpty() || shots.size()>12 || !QDir().mkpath(root)) return 2;
    const auto project=QDir(root).filePath("project");
    const auto imported=import_gaussian_ply(source,project,{});
    if (!imported.value("ok").toBool()) { std::cerr<<imported.value("error").toString().toStdString()<<'\n'; return 1; }
    const auto loaded=load_gaussian_scene(imported.value("artifactPath").toString(),{},imported.value("artifactValidation").toMap().value("artifactSha256").toString());
    if (!loaded.scene || !publish(QDir(root).filePath("plan-before.json"),bytes)) return 1;
    auto composition=load_gaussian_composition(project,loaded.scene->artifact_sha256);
    if (!composition.composition) return 1;
    if (plan.contains("composition")) {
      const auto authored=gaussian_scene_composition(plan.value("composition").toMap());
      if (!authored) return 2;
      const auto resources=resolve_gaussian_composition_resources(project,*authored);
      if (!resources.composition) { std::cerr<<resources.error.toStdString()<<'\n'; return 1; }
      composition=save_gaussian_composition(project,loaded.scene->artifact_sha256,*resources.composition,composition.receipt);
      if (!composition.composition) { std::cerr<<composition.error.toStdString()<<'\n'; return 1; }
    }
    auto renderer=make_gaussian_scene_renderer(); if (!renderer) return 1;
    QVariantList results;
    for (const auto& value:shots) {
      const auto shot=value.toMap(); const auto name=shot.value("name").toString();
      auto camera=gaussian_saved_camera(shot.value("camera").toMap());
      if (!camera || !QRegularExpression("^[a-z][a-z0-9-]{0,40}$").match(name).hasMatch()) return 2;
      if (shot.contains("physicalCamera")) {
        const auto physical=gaussian_physical_camera_settings(shot.value("physicalCamera").toMap());
        if (!physical) return 2;
        const auto built=make_gaussian_physical_camera(*camera,camera->image_size,*physical);
        if (!built.camera) { std::cerr<<built.error.toStdString()<<'\n'; return 1; }
        camera=*built.camera;
      }
      camera->composition=composition.composition;
      QVariantMap focus;
      if (shot.contains("focusUv")) {
        const auto uv=shot.value("focusUv").toList();
        if (uv.size()!=2) return 2;
        const double u=uv[0].toDouble(),v=uv[1].toDouble();
        if (!std::isfinite(u) || !std::isfinite(v) || u<0 || u>1 || v<0 || v>1) return 2;
        focus=pick_gaussian_focus(*loaded.scene,*camera,{u*camera->image_size.width(),v*camera->image_size.height()},{},{});
        if (!focus.value("ok").toBool()) { std::cerr<<focus.value("error").toString().toStdString()<<'\n'; return 1; }
        if (camera->physical) {
          auto physical=camera->physical->settings;
          physical.focus_distance_m=focus.value("focusDistance").toDouble()*physical.metres_per_scene_unit;
          const auto focused=make_gaussian_physical_camera(*camera,camera->image_size,physical,true);
          if (!focused.camera) { std::cerr<<focused.error.toStdString()<<'\n'; return 1; }
          camera=*focused.camera; camera->composition=composition.composition;
        } else camera->photo.lens.focus_distance=focus.value("focusDistance").toFloat();
      }
      std::cerr<<"Rendering "<<name.toStdString()<<'\n';
      const auto saved=save_gaussian_view(project,*loaded.scene,name,0,*camera);
      if (!saved.value("ok").toBool()) return 1;
      GaussianRenderOptions options;
      options.samples_completed=std::make_shared<std::atomic_int>(0);
      options.tiles_completed=std::make_shared<std::atomic_int>(0);
      options.export_tile_edge=shot.value("tileEdge",0).toInt();
      std::jthread reporter([options,name](std::stop_token stop) {
        const auto start=std::chrono::steady_clock::now();
        qint64 next=5000;
        while (!stop.stop_requested()) {
          const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
          if (ms>=next) {
            std::cerr<<name.toStdString()<<": "<<ms<<" ms, "<<options.tiles_completed->load()
                <<" tiles, "<<options.samples_completed->load()<<" aperture samples\n";
            next=ms+5000;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      });
      const auto frame=renderer->renderWithOptions(loaded.scene,*camera,true,{},options);
      reporter.request_stop();
      if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(QDir(root).filePath(name+".png"))) {
        std::cerr<<frame.error.toStdString()<<'\n'; return 1;
      }
      QVariantList crops;
      const auto requested_crops=shot.value("detailCrops").toList();
      if (requested_crops.size()>8) return 2;
      for (const auto& entry:requested_crops) {
        const auto crop=entry.toMap(); const auto label=crop.value("name").toString();
        const auto rect=crop.value("rect").toList();
        if (!QRegularExpression("^[a-z][a-z0-9-]{0,30}$").match(label).hasMatch() || rect.size()!=4) return 2;
        for (const auto& coordinate:rect) if (coordinate.toDouble()!=coordinate.toInt()) return 2;
        const QRect region(rect[0].toInt(),rect[1].toInt(),rect[2].toInt(),rect[3].toInt());
        const auto file=name+"-detail-"+label+".png";
        if (region.isEmpty() || region.width()>2048 || region.height()>2048 || !frame.image.rect().contains(region) ||
            !frame.image.copy(region).save(QDir(root).filePath(file))) return 2;
        crops.append(QVariantMap{{"image",file},{"rect",rect},{"resampled",false}});
      }
      results.append(QVariantMap{{"name",name},{"camera",camera->description()},{"focus",focus},{"metrics",frame.metrics},{"detailCrops",crops}});
      if (!publish(QDir(root).filePath(name+"-receipt.json"),QJsonDocument::fromVariant(results.last()).toJson())) return 2;
    }
    return publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},
        {"import",imported},{"plan",plan},{"shots",results},{"scope","Imported public capture and native photo rendering; source photographic detail requires separate review"}}).toJson()) ? 0 : 2;
  }
  if (args.value(1)=="--render-plan" || args.value(1)=="--film-plan" || args.value(1)=="--inspect-path") {
    if (args.size()!=5) return 2;
    const auto project=args[2],root=args[4]; QFile input(args[3]);
    if (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !input.open(QIODevice::ReadOnly) || input.size()>2*1024*1024) return 2;
    const auto bytes=input.readAll(); const auto plan=QJsonDocument::fromJson(bytes).toVariant().toMap();
    if (plan.value("schema")!="looksy-native-scene-staging-v1" || !QRegularExpression("^[0-9a-f]{64}$").match(plan.value("artifactSha256").toString()).hasMatch()) return 2;
    const auto camera=gaussian_saved_camera(plan.value("camera").toMap());
    const auto composition=gaussian_scene_composition(plan.value("composition").toMap());
    if (!camera || !composition || camera->image_size.width()>8192 || camera->image_size.height()>8192 || !QDir().mkpath(root)) return 2;
    const auto resources=resolve_gaussian_composition_resources(project,*composition);
    QFile manifest(QDir(project).filePath("scene.json"));
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    const bool imported_scene=QJsonDocument::fromJson(manifest.readAll()).object().value("importedArtifact").toBool();
    const auto loaded=load_gaussian_scene(QDir(project).filePath("trained/scene.ply"),imported_scene ? QString() : QDir(project).filePath("dataset/sparse"),plan.value("artifactSha256").toString());
    if (!resources.composition || !loaded.scene) { std::cerr<<resources.error.toStdString()<<loaded.error.toStdString()<<'\n'; return 1; }
    if (!publish(QDir(root).filePath("plan-before.json"),bytes)) return 2;
    auto view=*camera; view.composition=resources.composition;
    if (args[1]=="--film-plan" || args[1]=="--inspect-path") {
      const auto saved=load_gaussian_saved_views(project,*loaded.scene);
      QString error; auto path=gaussian_camera_path(saved.value("cameraPath").toMap(),saved.value("views").toList(),&error);
      if (!saved.value("ok").toBool() || !path || path->cameras.front().description()!=view.description() || path->segment_seconds!=plan.value("filmSeconds").toDouble()) {
        std::cerr<<"Saved path does not match the frozen plan: "<<error.toStdString()<<'\n';return 1;
      }
      for (auto& keyframe:path->cameras) keyframe.composition=resources.composition;
      if (!publish(QDir(root).filePath("path-before.json"),QJsonDocument::fromVariant(saved).toJson())) return 2;
      if (args[1]=="--inspect-path") {
        const int count=path->frameCount();
        if (count<2) return 1;
        QVariantList samples;
        for (const int index:{0,count/2,count-1}) {
          const double seconds=static_cast<double>(index)/path->fps;
          const auto sampled=path->sample(seconds);
          samples.append(QVariantMap{{"index",index},{"seconds",seconds},{"camera",sampled.description()}});
        }
        if (!publish(QDir(root).filePath("samples-before.json"),QJsonDocument::fromVariant(samples).toJson())) return 2;
        auto renderer=make_gaussian_scene_renderer();
        if (!renderer) return 1;
        QVariantList renders;
        for (const auto& value:samples) {
          const auto sample=value.toMap(); const int index=sample.value("index").toInt();
          const auto frame=renderer->render(loaded.scene,path->sample(sample.value("seconds").toDouble()),true);
          const auto name=QStringLiteral("frame-%1.png").arg(index,6,10,QChar('0'));
          if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(QDir(root).filePath(name))) {
            std::cerr<<frame.error.toStdString()<<'\n';return 1;
          }
          renders.append(QVariantMap{{"sample",sample},{"image",name},{"metrics",frame.metrics}});
          std::cerr<<"Inspected path frame "<<index<<'/'<<count<<'\n';
        }
        return publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{
            {"passed",true},{"frameCount",count},{"renders",renders},
            {"scope","First, middle and last export sample times from the actual saved path; not a complete film export"}}).toJson()) ? 0 : 2;
      }
      const auto progress=std::make_shared<GaussianFilmProgress>();
      std::jthread reporter([progress,count=path->frameCount()](std::stop_token stop) {
        int previous=-1;
        while (!stop.stop_requested()) {
          const int completed=progress->completed.load();
          if (completed!=previous) { std::cerr<<"Rendered "<<completed<<'/'<<count<<" frames\n";previous=completed; }
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
      });
      const int film_edge=plan.value("filmEdge",std::max(view.image_size.width(),view.image_size.height())).toInt();
      if (film_edge<256 || film_edge>8192) return 2;
      const auto film=export_gaussian_film(loaded.scene,*path,QDir(root).filePath("frames"),film_edge,{},progress,{},GaussianFilmFormat::PngSequence);
      reporter.request_stop();
      if (!film.value("ok").toBool()) { std::cerr<<film.value("error").toString().toStdString()<<'\n';return 1; }
      if (!publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"plan",plan},{"film",film}}).toJson())) return 2;
      return 0;
    }
    GaussianRenderOptions options;
    options.samples_completed=std::make_shared<std::atomic_int>(0);
    options.tiles_completed=std::make_shared<std::atomic_int>(0);
    options.lighting_batches_completed=std::make_shared<std::atomic_int>(0);
    const auto started=std::chrono::steady_clock::now();
    std::jthread reporter([options,started](std::stop_token stop) {
      qint64 next=0;
      while (!stop.stop_requested()) {
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        if (elapsed>=next) {
          const QVariantMap progress{{"elapsedMs",static_cast<qint64>(elapsed)},
              {"tilesCompleted",options.tiles_completed->load()},
              {"untiledApertureSamplesCompleted",options.samples_completed->load()},
              {"lightingBatchesCompleted",options.lighting_batches_completed->load()}};
          std::cerr<<QJsonDocument::fromVariant(progress).toJson(QJsonDocument::Compact).constData()<<'\n';
          next=elapsed+10000;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
      }
    });
    auto renderer=make_gaussian_scene_renderer();
    if (!renderer) return 1;
    if (plan.contains("filmVariants")) {
      const auto variants=plan.value("filmVariants").toList();
      if (variants.size()<2 || variants.size()>8) return 2;
      QVariantList renders;
      for (const auto& variant:variants) {
        const auto entry=variant.toMap(); const auto name=entry.value("name").toString();
        const auto film=gaussian_film_settings(entry.value("film").toMap());
        if (!film || !QRegularExpression("^[a-z][a-z0-9-]{0,40}$").match(name).hasMatch()) return 2;
        // Only the recording medium changes: camera, lens, scene and exposure
        // stay frozen, and completed optical samples may be reused exactly.
        view.photo.film=*film;
        const auto frame=renderer->renderWithOptions(loaded.scene,view,true,{},options);
        if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(QDir(root).filePath(name+".png"))) {
          std::cerr<<frame.error.toStdString()<<'\n'; return 1;
        }
        renders.append(QVariantMap{{"name",name},{"camera",view.description()},{"metrics",frame.metrics}});
      }
      reporter.request_stop();
      return publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{
          {"passed",true},{"plan",plan},{"renders",renders},
          {"scope","Same optical source with explicit film variants; not compound-lens integration or a measured film-stock match"}}).toJson()) ? 0 : 2;
    }
    const auto frame=renderer->renderWithOptions(loaded.scene,view,true,{},options);
    reporter.request_stop();
    if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(QDir(root).filePath("render.png"))) { std::cerr<<frame.error.toStdString()<<'\n';return 1; }
    if (!publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"plan",plan},{"metrics",frame.metrics}}).toJson())) return 2;
    return 0;
  }
  if (args.size()<3) { std::cerr<<"gaussian_scene_staging_driver PROJECT NEW_OUTPUT [--mode 0|1|2] [--edge N] [--camera-index N] [--probe-surfaces|--tabletop|--beauty] [--spatial] [--film] [--seconds N] [--yaw N]\n"
      <<"gaussian_scene_staging_driver --render-plan|--inspect-path|--film-plan PROJECT PLAN_JSON NEW_OUTPUT\n"; return 2; }
  const auto value = [&](const QString& key,const QString& fallback) { const auto at=args.indexOf(key); return at<0?fallback:args.value(at+1); };
  bool mode_ok=false,edge_ok=false,index_ok=false; const int mode=value("--mode","2").toInt(&mode_ok),edge=value("--edge","960").toInt(&edge_ok),camera_index=value("--camera-index","0").toInt(&index_ok);
  bool seconds_ok=false,yaw_ok=false; const double seconds=value("--seconds","0.5").toDouble(&seconds_ok); const float yaw=value("--yaw","1.5").toFloat(&yaw_ok);
  const auto root=args[2],project=QDir(root).filePath("project");
  if (!mode_ok || mode<0 || mode>2 || !edge_ok || edge<128 || edge>2048 || !index_ok || camera_index<0 || !seconds_ok || !std::isfinite(seconds) || seconds<0.5 || seconds>10 || !yaw_ok || !std::isfinite(yaw) || std::abs(yaw)>30 || !QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root)) return 2;
  const auto fail = [](const QString& reason) { std::cerr<<reason.toStdString()<<'\n'; return 1; };
  for (const auto* relative:{"scene.json","trained/scene.ply","dataset/sparse/cameras.bin","dataset/sparse/images.bin"}) {
    const auto target=QDir(project).filePath(QString::fromLatin1(relative));
    if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(QDir(args[1]).filePath(QString::fromLatin1(relative)),target)) return fail("Project input copy failed");
  }
  const auto ply=QDir(project).filePath("trained/scene.ply"); const auto validated=validate_gaussian_ply(ply,{});
  const auto hash=validated.value("artifactSha256").toString(); const auto loaded=load_gaussian_scene(ply,QDir(project).filePath("dataset/sparse"),hash);
  if (!loaded.scene || static_cast<size_t>(camera_index)>=loaded.scene->cameras.size()) return fail(loaded.error.isEmpty()?"Camera index unavailable":loaded.error);
  auto camera=loaded.scene->cameras[static_cast<size_t>(camera_index)]; camera=camera.resized({edge,qRound(static_cast<double>(edge)*camera.image_size.height()/camera.image_size.width())});
  const float focus=loaded.scene->focus_distance;
  const auto xyz=[](QVector3D p) {return QVariantList{p.x(),p.y(),p.z()};};
  if (args.contains("--probe-surfaces")) {
    QVariantList patches;
    for (const float v:{0.56F,0.60F,0.64F,0.68F}) for (const float u:{0.36F,0.44F,0.64F,0.72F}) {
      const auto found=patch(*loaded.scene,camera,u,v);
      if (found) patches.append(QVariantMap{{"uv",QVariantList{u,v}},{"position",xyz(found->position)},{"normal",xyz(found->normal)},
          {"upDot",QVector3D::dotProduct(found->normal,-camera.down)},{"towardCameraDot",QVector3D::dotProduct(found->normal,-camera.forward)}});
    }
    const auto frame=make_gaussian_scene_renderer()->render(loaded.scene,camera,true);
    if (!frame.error.isEmpty() || !frame.image.save(QDir(root).filePath("surface-reference.png")) ||
        !publish(QDir(root).filePath("surfaces.json"),QJsonDocument::fromVariant(QVariantMap{{"camera",camera.description()},{"patches",patches}}).toJson())) return fail("Surface probe failed");
    return 0;
  }
  const bool beauty=args.contains("--beauty"),tabletop=args.contains("--tabletop") || beauty;
  const auto ring_patch=patch(*loaded.scene,camera,tabletop?0.44F:0.39F,tabletop?0.60F:0.78F),sphere_patch=patch(*loaded.scene,camera,tabletop?0.72F:0.69F,tabletop?0.56F:0.74F);
  if (!ring_patch || !sphere_patch) return fail("No stable opaque staging surface at the authored anchors");
  const auto geometry=parse_gaussian_obj(torus()); if (!geometry.mesh) return fail(geometry.error);
  if (const auto error=store_gaussian_mesh_asset(project,geometry);!error.isEmpty()) return fail(error);
  GaussianSceneObject ring; ring.id="copper-ring"; ring.name="Copper torus"; ring.kind=2;
  ring.mesh=geometry.mesh; ring.mesh_sha256=geometry.mesh->sha256; ring.color=tabletop?QVector3D(0.95F,0.64F,0.54F):QVector3D(0.94F,0.61F,0.24F); ring.metallic=1; ring.roughness=0.18F;
  const auto rotation=QQuaternion::rotationTo({0,1,0},camera.forward); ring.rotation=rotation.toEulerAngles();
  ring.size=QVector3D(1,1,1)*(focus*(tabletop?0.035F:0.05F)/0.92F);
  float lowest=1e30F;
  for (const auto& triangle:ring.mesh->triangles) {
    const auto p=gaussian_mesh_vector(triangle.position);
    for (const auto& vertex:{p,p+gaussian_mesh_vector(triangle.edge1),p+gaussian_mesh_vector(triangle.edge2)})
      lowest=std::min(lowest,QVector3D::dotProduct(ring_patch->normal,rotation.rotatedVector(vertex*ring.size.x())));
  }
  ring.position=ring_patch->position-ring_patch->normal*lowest;
  GaussianSceneObject ball; ball.id="chrome-sphere"; ball.name="Chrome sphere"; ball.size=QVector3D(1,1,1)*(focus*(tabletop?0.035F:0.04F));
  ball.position=sphere_patch->position+sphere_patch->normal*ball.size.x(); ball.color={0.9F,0.9F,0.9F}; ball.metallic=1; ball.roughness=0.09F;
  GaussianSceneComposition composition; composition.objects={ring,ball}; composition.ambient=tabletop?0.04F:0.15F; composition.capture_lighting=mode;
  composition.reflections=true; composition.environment_diffuse=true; composition.environment_strength=0.8F; composition.scan_shadows=true;
  composition.shadow_strength=0.6F; composition.light_samples=32;
  GaussianSceneLight key; key.id="window-key"; key.name="Soft window key"; key.vector=(-camera.right*0.6F-camera.down*0.8F-camera.forward*0.2F).normalized();
  key.intensity=2.2F; key.angular_radius=4; composition.lights={key};
  if (args.contains("--spatial")) { composition.spatial_reflections=true; composition.spatial_environment=true; }
  if (beauty) {
    camera.photo.focal_scale=1.4F; camera.cx-=camera.image_size.width()*0.112F; camera.cy+=camera.image_size.height()*0.14F;
    camera.photo.lens.focus_distance=(ring_patch->depth+sphere_patch->depth)*0.5F;
    camera.photo.lens.aperture_radius=camera.photo.lens.focus_distance*0.012F; camera.photo.lens.samples=64;
    camera.photo.tone.contrast=1.04F; camera.photo.tone.highlight_compression=0.08F;
    camera.photo.film.halation_amount=0.04F; camera.photo.film.halation_radius=2.5F;
    composition.haze_density=0.02F/focus; composition.haze_distance=focus*3; composition.haze_shadows=true;
    composition.haze_color={0.62F,0.69F,0.8F}; composition.haze_anisotropy=0.2F; composition.light_samples=128;
  }
  const QVariantMap plan{{"schema","looksy-native-scene-staging-v1"},{"sourceProject",args[1]},{"artifactSha256",hash},{"captureMode",mode},{"edge",edge},
      {"ringAnchor",QVariantMap{{"position",xyz(ring_patch->position)},{"normal",xyz(ring_patch->normal)}}},
      {"sphereAnchor",QVariantMap{{"position",xyz(sphere_patch->position)},{"normal",xyz(sphere_patch->normal)}}},
      {"composition",composition.description()},{"camera",camera.description()},{"cameraIndex",camera_index},{"tabletop",tabletop},{"beauty",beauty},
      {"filmSeconds",seconds},{"filmYawDegrees",yaw},{"scope","Authored native composition on a real scan, not a matched photographic ground truth"}};
  if (!publish(QDir(root).filePath("plan-before.json"),QJsonDocument::fromVariant(plan).toJson())) return fail("Cannot publish staging plan");
  auto renderer=make_gaussian_scene_renderer(); QVariantList renders;
  const auto render=[&](const QString& name,const std::shared_ptr<const GaussianSceneComposition>& configured) {
    auto view=camera; view.composition=configured; const auto frame=renderer->render(loaded.scene,view,true);
    if (!frame.error.isEmpty() || frame.image.isNull()) {std::cerr<<name.toStdString()<<": "<<frame.error.toStdString()<<'\n';return false;}
    if (!frame.image.save(QDir(root).filePath(name+".png"))) return false;
    renders.append(QVariantMap{{"name",name},{"metrics",frame.metrics}}); std::cout<<name.toStdString()<<" rendered\n"<<std::flush;return true;
  };
  if (!render("source-neutral",{})) return fail("Neutral render failed");
  auto background=composition; background.objects.clear(); background.reflections=false; background.environment_diffuse=false;
  if (!render("mode-background",std::make_shared<GaussianSceneComposition>(background))) return fail("Background mode render failed");
  auto captured=composition; captured.capture_lighting=0;
  if (!render("objects-captured",std::make_shared<GaussianSceneComposition>(captured))) return fail("Captured composition failed");
  if (!render("finished",std::make_shared<GaussianSceneComposition>(composition))) return fail("Finished composition failed");
  const auto saved=save_gaussian_composition(project,hash,composition,{}); if (!saved.composition) return fail(saved.error);
  camera.composition=std::make_shared<GaussianSceneComposition>(composition);
  const auto shot=save_gaussian_view(project,*loaded.scene,"Staged scan A",camera_index,camera);
  auto second=navigate_gaussian_camera(camera,focus,yaw,0,0,0,0); second.composition=camera.composition;
  const auto shot_b=save_gaussian_view(project,*loaded.scene,"Staged scan B",camera_index,second);
  const QVariantMap settings{{"viewIds",QVariantList{shot.value("id"),shot_b.value("id")}},{"segmentSeconds",seconds},{"fps",24}};
  if (!shot.value("ok").toBool() || !shot_b.value("ok").toBool() || !save_gaussian_camera_path(project,*loaded.scene,settings).value("ok").toBool()) return fail("Staging shots/path save failed");
  QVariantMap film;
  if (args.contains("--film")) {
    GaussianCameraPath path; path.cameras={camera,second}; path.segment_seconds=seconds; path.fps=24;
    film=export_gaussian_film(loaded.scene,path,QDir(root).filePath("frames"),edge,{},std::make_shared<GaussianFilmProgress>(),{},GaussianFilmFormat::PngSequence);
    if (!film.value("ok").toBool()) return fail(film.value("error").toString());
  }
  if (!publish(QDir(root).filePath("receipt.json"),QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"plan",plan},{"renders",renders},{"film",film}}).toJson())) return fail("Cannot publish staging receipt");
  return 0;
}
