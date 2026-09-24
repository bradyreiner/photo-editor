#include "gaussian_scene_renderer.h"
#include "gaussian_spacetime.h"
#include "gaussian_spacetime_artifact.h"
#include "gaussian_ply_validation.h"
#include <QGuiApplication>
#include <QColorSpace>
#include <QQuaternion>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <numbers>

using namespace photo_editor::desktop;
namespace {
GaussianPoint appearance(std::array<float,3> rgb) {
  GaussianPoint point;
  for(size_t c=0;c<3;++c) point.sh[c]=(rgb[c]-0.5F)/0.2820947918F;
  return point;
}
struct Input { std::array<double,3> mean, velocity, variance; double time, time_variance, opacity; GaussianPoint color; };
// Throughput probe only: preserve the real static trainer's latent ellipsoids
// when adding synthetic motion, instead of trying to refactor rounded float
// covariances. This does not import or fabricate a trained 4D checkpoint.
bool read_static_factors(const QString& artifact, std::vector<GaussianTemporalPoint>* points) {
  QFile file(artifact); if(!file.open(QIODevice::ReadOnly)) return false;
  QStringList properties;
  while(!file.atEnd()) {
    const auto line=file.readLine();
    if(line.startsWith("property ")) properties.append(QString::fromLatin1(line.simplified().split(' ').last()));
    if(line.trimmed()=="end_header") break;
  }
  const std::array<qsizetype,7> indices{properties.indexOf("scale_0"),properties.indexOf("scale_1"),properties.indexOf("scale_2"),
      properties.indexOf("rot_0"),properties.indexOf("rot_1"),properties.indexOf("rot_2"),properties.indexOf("rot_3")};
  for(const auto index:indices) if(index<0) return false;
  const auto row_bytes=properties.size()*4;
  for(size_t first=0;first<points->size();first+=4096) {
    const auto rows=std::min(size_t{4096},points->size()-first);
    const auto bytes=file.read(static_cast<qint64>(rows)*row_bytes);
    if(bytes.size()!=static_cast<qint64>(rows)*row_bytes) return false;
    for(size_t row=0;row<rows;++row) {
      const auto value=[&](size_t index) { return std::bit_cast<float>(qFromLittleEndian<quint32>(bytes.constData()+static_cast<qsizetype>(row)*row_bytes+indices[index]*4)); };
      const auto rotation=QQuaternion(value(3),value(4),value(5),value(6)).normalized().toRotationMatrix();
      auto& factor=(*points)[first+row].covariance_factor;
      for(size_t axis=0;axis<3;++axis) for(size_t r=0;r<3;++r)
        factor[axis*4+r]=static_cast<float>(std::exp(double(value(axis)))*rotation(static_cast<int>(r),static_cast<int>(axis)));
    }
  }
  return file.atEnd();
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  if (argc!=1 && argc!=2 && argc!=4) return 2;
  QTemporaryDir temporary;
  const auto root=argc>1 ? QString::fromLocal8Bit(argv[1]) : temporary.path();
  if(argc>1 && (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root))) return 2;
  auto renderer=make_gaussian_scene_renderer(); if(!renderer) return 1;
  GaussianCamera camera; camera.name="camera-a"; camera.image_size={129,97}; camera.fx=camera.fy=105; camera.cx=64.5F; camera.cy=48.5F;
  std::array<Input,3> inputs{{{{0,0,3},{0.15,0,1},{0.07,0.025,0.05},0,2,0.65,appearance({0.8F,0.1F,0.1F})},
      {{0,0,3},{-0.1,0.1,-1},{0.04,0.08,0.03},0,1,0.7,appearance({0.1F,0.1F,0.8F})},
      {{0.8,0.3,4},{-0.4,0,0},{0.06,0.03,0.01},0.5,0.03,0.9,appearance({0.1F,0.7F,0.1F})}}};
  auto scene=std::make_shared<GaussianSceneData>();
  scene->time_origin_seconds=86400; scene->local_time_range={-1,1}; scene->sh_degree=3; scene->temporal_sh_degree=2;
  scene->cameras.push_back(camera); scene->focus_distance=3;
  for(size_t i=0;i<inputs.size();++i) {
    auto& source=inputs[i]; source.color.sh[6]=0.12F; source.color.sh[30]=0.1F;
    std::array<double,16> covariance{};
    for(size_t r=0;r<3;++r) {
      for(size_t c=0;c<3;++c) covariance[r*4+c]=(r==c ? source.variance[r] : 0)+source.velocity[r]*source.velocity[c]*source.time_variance;
      covariance[r*4+3]=covariance[12+r]=source.velocity[r]*source.time_variance;
    }
    covariance[15]=source.time_variance;
    const auto compiled=compile_gaussian_spacetime({source.mean[0],source.mean[1],source.mean[2],source.time},covariance,source.opacity,source.color.sh);
    if(!compiled) return 1;
    scene->points.push_back(compiled->spatial); scene->temporal.push_back(compiled->temporal);
    GaussianTemporalAppearance time_color; time_color.sh[0]=0.15F; time_color.sh[48+1]=0.12F;
    time_color.sh[3*12+2]=0.08F; scene->temporal_appearance.push_back(time_color);
  }
  if(renderer->render(scene,camera,true).error.isEmpty()) return 1;
  int maximum_error=0,frames=0,artifact_frames=0;
  double max_ms=0;
  QVariantList metrics;
  for(int spatial_degree=0;spatial_degree<=3;++spatial_degree) for(int temporal_degree=0;temporal_degree<=2;++temporal_degree) {
    auto variant=std::make_shared<GaussianSceneData>(*scene);
    variant->sh_degree=spatial_degree; variant->temporal_sh_degree=temporal_degree;
    if(temporal_degree==0) variant->temporal_appearance.clear();
    const auto artifact_path=QDir(root).filePath(QStringLiteral("scene-%1-%2.safetensors").arg(spatial_degree).arg(temporal_degree));
    const auto saved=write_gaussian_spacetime_artifact(*variant,artifact_path);
    const auto reloaded=load_gaussian_spacetime_artifact(artifact_path,saved.value("artifactSha256").toString());
    if(!saved.value("ok").toBool() || !reloaded.scene) { std::cerr<<reloaded.error.toStdString(); return 1; }
    for(int view=0;view<2;++view) for(const double time : {-1.0,-0.5,0.0,0.5,1.0,1.0/120.0}) {
    const auto pose=view==0 ? camera : navigate_gaussian_camera(camera,3,18,8,0,0,0);
    // Independent known-factor construction: no production Schur compiler or
    // temporal center helper is used to produce this static oracle.
    auto reference=std::make_shared<GaussianSceneData>(); reference->sh_degree=spatial_degree;
    for(size_t i=0;i<inputs.size();++i) {
      const auto& source=inputs[i]; auto point=source.color; const double dt=time-source.time;
      for(size_t axis=0;axis<3;++axis) point.center_opacity[axis]=static_cast<float>(source.mean[axis]+source.velocity[axis]*dt);
      point.center_opacity[3]=static_cast<float>(source.opacity*std::exp(-0.5*dt*dt/source.time_variance));
      point.covariance_a={static_cast<float>(source.variance[0]),0,0,static_cast<float>(source.variance[1])};
      point.covariance_b={0,static_cast<float>(source.variance[2]),0,0};
      for(size_t order=1;order<=static_cast<size_t>(temporal_degree);++order) for(size_t coefficient=0;coefficient<48;++coefficient)
        point.sh[coefficient]+=static_cast<float>(std::cos(2*std::numbers::pi*double(order)*dt/2)*scene->temporal_appearance[i].sh[(order-1)*48+coefficient]);
      reference->points.push_back(point);
    }
    const auto actual=renderer->render(variant,pose,true,scene->time_origin_seconds+time);
    if(actual.metrics.value("depthCenterBytes").toULongLong()!=0) return 1;
    const auto restored=renderer->render(reloaded.scene,pose,true,reloaded.scene->time_origin_seconds+time);
    if(!restored.error.isEmpty() || restored.image.isNull() || restored.image!=actual.image ||
        restored.image.colorSpace()!=actual.image.colorSpace()) return 1;
    ++artifact_frames;
    const auto expected=renderer->render(reference,pose,true);
    if(!actual.error.isEmpty() || !expected.error.isEmpty()) { std::cerr << actual.error.toStdString() << expected.error.toStdString(); return 1; }
    if(!actual.metrics.value("temporal").toBool() || actual.image.colorSpace()!=expected.image.colorSpace()) return 1;
    for(int y=0;y<actual.image.height();++y) for(int x=0;x<actual.image.width();++x) {
      const auto a=actual.image.pixelColor(x,y),b=expected.image.pixelColor(x,y);
      maximum_error=std::max({maximum_error,std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
    }
    if(!actual.image.save(QDir(root).filePath(QStringLiteral("view-%1-frame-%2.png").arg(view).arg(frames)))) return 2;
    metrics.append(actual.metrics); ++frames;
    }
  }
  // Consecutive times on the same immutable GPU scene exercise buffer reuse;
  // previews must keep native textures and avoid CPU readback.
  for(int frame=0;frame<30;++frame) {
    const auto rendered=renderer->render(scene,camera,false,scene->time_origin_seconds-1+double(frame)*2/29);
    if(!rendered.error.isEmpty() || !rendered.native_texture || !rendered.native_lease || !rendered.image.isNull()) return 1;
    max_ms=std::max(max_ms,rendered.metrics.value("renderMs").toDouble());
  }
  int invalid=0;
  for(int mode=0;mode<10;++mode) {
    auto bad=std::make_shared<GaussianSceneData>(*scene); double time=86400;
    if(mode==0) time=86402;
    if(mode==1) time=std::numeric_limits<double>::quiet_NaN();
    if(mode==2) bad->temporal.pop_back();
    if(mode==3) bad->temporal_sh_degree=3;
    if(mode==4) bad->temporal_appearance.clear();
    if(mode==5) bad->temporal[0].normalization[0]=0;
    if(mode==6) bad->local_time_range={1,1};
    if(mode==7) bad->temporal[0].covariance_factor.fill(0);
    if(mode==8) bad->points[0].sh[0]=std::numeric_limits<float>::quiet_NaN();
    if(mode==9) bad->temporal[0].velocity_time[0]=std::numeric_limits<float>::infinity();
    if(renderer->render(bad,camera,true,time).error.isEmpty()) return 1;
    ++invalid;
  }
  QVariantMap large_proof;
  if(argc==4) {
    const auto artifact=QString::fromLocal8Bit(argv[2]);
    const auto verified=validate_gaussian_ply(artifact,{});
    const auto loaded=load_gaussian_scene(artifact,QString::fromLocal8Bit(argv[3]),verified.value("artifactSha256").toString());
    if(!loaded.scene) { std::cerr<<loaded.error.toStdString(); return 1; }
    auto large=std::make_shared<GaussianSceneData>(*loaded.scene);
    large->local_time_range={0,1}; large->temporal.resize(large->points.size());
    if(!read_static_factors(artifact,&large->temporal)) return 1;
    size_t invalid_conditionals=0;
    for(const auto& point : large->points) if(!valid_gaussian_conditional(point)) {
      if(invalid_conditionals==0) {
        std::cerr << "First rejected static covariance:";
        for(const auto v:point.covariance_a) std::cerr << ' ' << v;
        for(const auto v:point.covariance_b) std::cerr << ' ' << v;
        std::cerr << " opacity=" << point.center_opacity[3] << '\n';
      }
      ++invalid_conditionals;
    }
    std::cerr << "Unrepresentable SPD float covariances=" << invalid_conditionals << '/' << large->points.size() << '\n';
    for(size_t i=0;i<large->temporal.size();++i) {
      const auto sign=(i%2==0 ? 1.0F : -1.0F);
      large->temporal[i].velocity_time={0.02F*sign,0.01F*sign,0.03F*sign,0.5F};
      large->temporal[i].normalization[0]=0.25F;
    }
    std::vector<double> timings;
    const auto pose=large->cameras.front();
    for(int i=0;i<17;++i) {
      const double time=double(i)/16;
      const auto frame=renderer->render(large,pose,false,time);
      if(!frame.error.isEmpty() || !frame.native_texture || !frame.image.isNull()) { std::cerr<<frame.error.toStdString(); return 1; }
      if(i>0) timings.push_back(frame.metrics.value("renderMs").toDouble());
    }
    std::sort(timings.begin(),timings.end());
    large_proof={{"pointCount",static_cast<qulonglong>(large->points.size())},{"frames",17},
        {"roundedCovariancesRequiringFactors",static_cast<qulonglong>(invalid_conditionals)},
        {"medianWorkerRenderMs",timings[timings.size()/2]},{"maximumWorkerRenderMs",timings.back()},
        {"width",pose.image_size.width()},{"height",pose.image_size.height()},
        {"scope","synthetic-motion-added-to-real-static-trained-scene; not dynamic reconstruction or foreground FPS"}};
  }
  const QVariantMap receipt{{"ok",maximum_error<=2},{"scope","synthetic-full-4d-conditioning-and-metal-time-rendering-not-training"},
      {"oracleFrames",frames},{"maximumChannelError",maximum_error},{"artifactPixelExactFrames",artifact_frames},{"invalidRequestsRejected",invalid+1},
      {"nativePreviewFrames",30},{"maximumThreePointPreviewMs",max_ms},{"frames",metrics},{"largeSceneProbe",large_proof}};
  QFile output(QDir(root).filePath("receipt.json"));
  if(!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(QJsonDocument::fromVariant(receipt).toJson())<0) return 2;
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return maximum_error<=2 ? 0 : 1;
}
