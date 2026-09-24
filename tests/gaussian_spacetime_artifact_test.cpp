#include "gaussian_spacetime_artifact.h"
#include "gaussian_spacetime.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
QString hash(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
QByteArray read(const QString& path) { QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool write(const QString& path,const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes)==bytes.size(); }
QByteArray packed(const QJsonObject& header,const QByteArray& payload) {
  const auto bytes=QJsonDocument(header).toJson(QJsonDocument::Compact);
  QByteArray result(8,'\0'); qToLittleEndian<quint64>(static_cast<quint64>(bytes.size()),result.data());
  return result+bytes+payload;
}
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  QTemporaryDir temporary;
  if(argc==4 && QString::fromLocal8Bit(argv[1])=="--verify") {
    const auto loaded=load_gaussian_spacetime_artifact(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]));
    if(!loaded.scene) { std::cerr<<loaded.error.toStdString(); return 1; }
    const auto result=write_gaussian_spacetime_artifact(*loaded.scene,QDir(temporary.path()).filePath("canonical.safetensors"));
    std::cout<<QJsonDocument::fromVariant(result).toJson().constData();
    return result.value("ok").toBool() ? 0 : 1;
  }
  const auto root=argc==2 ? QString::fromLocal8Bit(argv[1]) : temporary.path();
  if(argc>2 || (argc==2 && (!QFileInfo(root).isAbsolute() || QFileInfo::exists(root) || !QDir().mkpath(root)))) return 2;
  GaussianSceneData scene;
  scene.time_origin_seconds=86400; scene.local_time_range={-1,1}; scene.focus_distance=3;
  GaussianCamera camera; camera.name="camera-a"; camera.image_size={129,97}; camera.fx=camera.fy=105; camera.cx=64.5F; camera.cy=48.5F;
  scene.cameras.push_back(camera);
  const std::array<double,16> covariance{0.19,0,0,0.4, 0,0.04,0,0, 0,0,0.1,0, 0.4,0,0,2};
  const auto point=compile_gaussian_spacetime({0,0,3,0.25},covariance,0.8);
  if(!point) return 1;
  scene.points={point->spatial,point->spatial}; scene.temporal={point->temporal,point->temporal};
  scene.temporal[1].velocity_time[2]=0.1F;
  for(size_t i=0;i<48;++i) scene.points[0].sh[i]=static_cast<float>(i)*0.003F;
  int roundtrips=0;
  QString final_path;
  QByteArray final_bytes;
  for(int spatial=0;spatial<=3;++spatial) for(int temporal=0;temporal<=2;++temporal) {
    scene.sh_degree=spatial; scene.temporal_sh_degree=temporal;
    scene.temporal_appearance.clear();
    if(temporal>0) {
      scene.temporal_appearance.resize(scene.points.size());
      for(size_t i=0;i<96;++i) scene.temporal_appearance[0].sh[i]=static_cast<float>(i)*0.0005F;
    }
    const auto path=QDir(root).filePath(QStringLiteral("scene-%1-%2.safetensors").arg(spatial).arg(temporal));
    const auto result=write_gaussian_spacetime_artifact(scene,path);
    if(!result.value("ok").toBool()) { std::cerr<<result.value("error").toString().toStdString(); return 1; }
    const auto bytes=read(path);
    if(hash(bytes)!=result.value("artifactSha256").toString()) return 1;
    const auto loaded=load_gaussian_spacetime_artifact(path,hash(bytes));
    if(!loaded.scene || loaded.scene->points.size()!=2 || loaded.scene->sh_degree!=spatial ||
        loaded.scene->temporal_sh_degree!=temporal || loaded.scene->time_origin_seconds!=86400 ||
        loaded.scene->local_time_range!=scene.local_time_range || loaded.scene->cameras[0].description()!=camera.description()) {
      std::cerr<<loaded.error.toStdString(); return 1;
    }
    for(size_t i=0;i<2;++i) {
      if(loaded.scene->points[i].center_opacity!=scene.points[i].center_opacity || loaded.scene->points[i].sh!=scene.points[i].sh ||
          loaded.scene->temporal[i].velocity_time!=scene.temporal[i].velocity_time ||
          loaded.scene->temporal[i].normalization!=scene.temporal[i].normalization ||
          loaded.scene->temporal[i].covariance_factor!=scene.temporal[i].covariance_factor ||
          (temporal>0 && loaded.scene->temporal_appearance[i].sh!=scene.temporal_appearance[i].sh)) return 1;
    }
    if(write_gaussian_spacetime_artifact(scene,path).value("ok").toBool() || read(path)!=bytes) return 1;
    final_path=path; final_bytes=bytes; ++roundtrips;
  }
  const auto header_length=static_cast<qsizetype>(qFromLittleEndian<quint64>(final_bytes.constData()));
  const auto header=QJsonDocument::fromJson(final_bytes.mid(8,header_length)).object();
  const auto payload=final_bytes.mid(8+header_length);
  const auto bad_path=QDir(root).filePath("invalid.safetensors");
  int invalid=0;
  const auto reject=[&](const QByteArray& bytes) {
    if(!write(bad_path,bytes) || load_gaussian_spacetime_artifact(bad_path,hash(bytes)).scene) return false;
    ++invalid; return true;
  };
  for(int mode=0;mode<17;++mode) {
    auto bad=header; auto tensor=bad.value("gaussians").toObject(); auto shape=tensor.value("shape").toArray();
    auto meta=bad.value("__metadata__").toObject();
    auto description=QJsonDocument::fromJson(meta.value("framehouse.scene").toString().toUtf8()).object();
    if(mode==0) tensor["dtype"]="F64";
    if(mode==1) shape[0]=2000001;
    if(mode==2) shape[0]="2";
    if(mode==3) shape[1]=163;
    if(mode==4) tensor["data_offsets"]=QJsonArray{1,payload.size()+1};
    if(mode==5) tensor["data_offsets"]=QJsonArray{0,payload.size()-1};
    if(mode==6) description["schema"]="unknown";
    if(mode==7) description["timeUnit"]="frames";
    if(mode==8) description["worldUnit"]="millimetres";
    if(mode==9) description["colorSpace"]="linear";
    if(mode==10) description["localTimeRange"]=QJsonArray{1,1};
    if(mode==11) description["temporalDegree"]=3;
    if(mode==12) description["spatialDegree"]=4;
    if(mode==13) description["timeOriginSeconds"]="86400";
    if(mode==14) description["cameras"]=QJsonArray{};
    if(mode==15) { auto cameras=description["cameras"].toArray(); auto cam=cameras[0].toObject(); cam["right"]=QJsonArray{-1,0,0}; cameras[0]=cam; description["cameras"]=cameras; }
    if(mode==16) description["layout"]="unknown";
    tensor["shape"]=shape; bad["gaussians"]=tensor;
    meta["framehouse.scene"]=QString::fromUtf8(QJsonDocument(description).toJson(QJsonDocument::Compact)); bad["__metadata__"]=meta;
    if(!reject(packed(bad,payload))) { std::cerr<<"Bad header accepted mode="<<mode; return 1; }
  }
  if(!reject(final_bytes.first(final_bytes.size()-1)) || !reject(final_bytes+'x')) return 1;
  for(const size_t column:{size_t{0},size_t{3},size_t{8},size_t{9},size_t{18}}) {
    auto bytes=final_bytes; qToLittleEndian<quint32>(std::bit_cast<quint32>(std::numeric_limits<float>::quiet_NaN()),bytes.data()+8+header_length+static_cast<qsizetype>(column*4));
    if(!reject(bytes)) return 1;
  }
  auto duplicate=QJsonDocument(header).toJson(QJsonDocument::Compact);
  duplicate.insert(1,"\"gaussians\":{},");
  QByteArray duplicate_prefix(8,'\0'); qToLittleEndian<quint64>(static_cast<quint64>(duplicate.size()),duplicate_prefix.data());
  if(!reject(duplicate_prefix+duplicate+payload)) return 1;
  duplicate=QJsonDocument(header).toJson(QJsonDocument::Compact);
  duplicate.insert(1,"\"gauss\\u0069ans\":{},");
  qToLittleEndian<quint64>(static_cast<quint64>(duplicate.size()),duplicate_prefix.data());
  if(!reject(duplicate_prefix+duplicate+payload)) return 1;
  auto nested=header;
  auto nested_meta=nested["__metadata__"].toObject();
  auto nested_text=nested_meta["framehouse.scene"].toString().toUtf8();
  nested_text.insert(1,"\"schema\":\"ambiguous\",");
  nested_meta["framehouse.scene"]=QString::fromUtf8(nested_text); nested["__metadata__"]=nested_meta;
  if(!reject(packed(nested,payload))) return 1;
  const auto linked_path=QDir(root).filePath("linked.safetensors");
  if(!QFile::link(final_path,linked_path) || load_gaussian_spacetime_artifact(linked_path,hash(final_bytes)).scene ||
      write_gaussian_spacetime_artifact(scene,linked_path).value("ok").toBool() || read(final_path)!=final_bytes) return 1;
  if(load_gaussian_spacetime_artifact(final_path,QString(64,'0')).scene || load_gaussian_spacetime_artifact(final_path,{}).scene) return 1;
  auto cancel=std::make_shared<std::atomic_bool>(true);
  const auto canceled_path=QDir(root).filePath("canceled.safetensors");
  if(load_gaussian_spacetime_artifact(final_path,hash(final_bytes),cancel).scene ||
      write_gaussian_spacetime_artifact(scene,canceled_path,cancel).value("ok").toBool() || QFileInfo::exists(canceled_path)) return 1;
  auto streamed=scene;
  streamed.points.resize(8257,scene.points[0]); streamed.temporal.resize(8257,scene.temporal[0]);
  streamed.temporal_appearance.resize(8257,scene.temporal_appearance[0]);
  for(size_t i=0;i<streamed.points.size();++i) streamed.points[i].center_opacity[0]=static_cast<float>(i)*0.001F;
  const auto stream_path=QDir(root).filePath("streamed.safetensors");
  const auto stream_result=write_gaussian_spacetime_artifact(streamed,stream_path);
  const auto stream_loaded=load_gaussian_spacetime_artifact(stream_path,stream_result.value("artifactSha256").toString());
  if(!stream_result.value("ok").toBool() || !stream_loaded.scene || stream_loaded.scene->points.size()!=8257) return 1;
  for(size_t i=0;i<streamed.points.size();++i) if(stream_loaded.scene->points[i].center_opacity!=streamed.points[i].center_opacity ||
      stream_loaded.scene->temporal[i].covariance_factor!=streamed.temporal[i].covariance_factor ||
      stream_loaded.scene->temporal_appearance[i].sh!=streamed.temporal_appearance[i].sh) return 1;
  const QVariantMap receipt{{"ok",true},{"roundTrips",roundtrips},{"invalidArtifactsRejected",invalid},{"symlinkRejected",true},
      {"streamedPoints",8257},
      {"artifactPath",final_path},{"artifactSha256",hash(final_bytes)},{"scope","synthetic-data-only-artifact-roundtrip-not-checkpoint-conversion"}};
  const auto output=QJsonDocument::fromVariant(receipt).toJson();
  if(!write(QDir(root).filePath("receipt.json"),output)) return 2;
  std::cout<<output.constData();
  return 0;
}
