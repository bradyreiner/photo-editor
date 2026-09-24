#include "gaussian_spacetime_artifact.h"
#include "gaussian_saved_views.h"
#include "gaussian_spacetime.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>
#include <bit>

namespace photo_editor::desktop {
namespace {
constexpr qint64 max_header = 1024 * 1024;
constexpr size_t max_points = 2000000;
bool stopped(const std::shared_ptr<std::atomic_bool>& flag) { return flag && flag->load(); }
bool number(const QJsonValue& value, double minimum, double maximum) {
  return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= minimum && value.toDouble() <= maximum;
}
// QJson validates grammar but accepts duplicate object keys. Reject duplicates
// (including escaped aliases) before interpreting either JSON layer.
bool unique_keys(const QByteArray& bytes) {
  struct Scope { bool object, key; QSet<QString> keys; };
  std::vector<Scope> stack;
  for (qsizetype i=0;i<bytes.size();++i) {
    const auto ch=bytes[i];
    if(ch=='{' || ch=='[') stack.push_back({ch=='{',ch=='{',{}});
    else if(ch=='}' || ch==']') { if(stack.empty()) return false; stack.pop_back(); }
    else if(ch==',' && !stack.empty() && stack.back().object) stack.back().key=true;
    else if(ch=='"') {
      const auto first=i;
      for(++i;i<bytes.size();++i) {
        if(bytes[i]=='\\') ++i;
        else if(bytes[i]=='"') break;
      }
      if(i>=bytes.size()) return false;
      if(!stack.empty() && stack.back().object && stack.back().key) {
        const auto token=QJsonDocument::fromJson('['+bytes.mid(first,i-first+1)+']').array()[0].toString();
        if(stack.back().keys.contains(token)) return false;
        stack.back().keys.insert(token); stack.back().key=false;
      }
    }
  }
  return stack.empty();
}
std::optional<QJsonObject> object(const QByteArray& bytes) {
  QJsonParseError error; const auto doc=QJsonDocument::fromJson(bytes,&error);
  if(error.error!=QJsonParseError::NoError || !doc.isObject() || !unique_keys(bytes)) return {};
  return doc.object();
}
bool metadata(const QJsonObject& value, GaussianSceneData* scene) {
  if(value.value("schema")!="framehouse-gaussian-spacetime-v1" || value.value("worldUnit")!="metres" ||
      value.value("timeUnit")!="seconds" || value.value("colorSpace")!="sRGB-display-referred" ||
      value.value("layout")!="xyz-opacity-velocity-time-inverse-sigma-factor-column-major-sh-rgb-v1" ||
      value.value("pixelConvention")!="top-left-corner") return false;
  for(const auto* key : {"spatialDegree","temporalDegree"}) {
    if(!number(value.value(key),0,key==QString("spatialDegree") ? 3 : 2) ||
        value.value(key).toDouble()!=std::floor(value.value(key).toDouble())) return false;
  }
  if(!number(value.value("timeOriginSeconds"),-1e12,1e12) || !number(value.value("focusDistance"),1e-6,1e6)) return false;
  const auto range=value.value("localTimeRange").toArray();
  if(range.size()!=2 || !number(range[0],-86400,86400) || !number(range[1],-86400,86400) ||
      range[1].toDouble()-range[0].toDouble()<1e-6 || range[1].toDouble()-range[0].toDouble()>86400) return false;
  scene->sh_degree=value.value("spatialDegree").toInt(); scene->temporal_sh_degree=value.value("temporalDegree").toInt();
  scene->time_origin_seconds=value.value("timeOriginSeconds").toDouble();
  scene->local_time_range={static_cast<float>(range[0].toDouble()),static_cast<float>(range[1].toDouble())};
  if(scene->local_time_range[1]<=scene->local_time_range[0]) return false;
  scene->focus_distance=static_cast<float>(value.value("focusDistance").toDouble());
  const auto cameras=value.value("cameras").toArray();
  if(cameras.isEmpty() || cameras.size()>2000) return false;
  QSet<QString> names;
  for(const auto& camera_value:cameras) {
    const auto camera=gaussian_saved_camera(camera_value.toObject().toVariantMap());
    if(!camera || camera->name.isEmpty() || camera->name.size()>128 || names.contains(camera->name)) return false;
    names.insert(camera->name); scene->cameras.push_back(*camera);
  }
  return true;
}
QJsonObject metadata(const GaussianSceneData& scene) {
  QJsonArray cameras;
  for(const auto& camera:scene.cameras) cameras.append(QJsonObject::fromVariantMap(camera.description()));
  return {{"schema","framehouse-gaussian-spacetime-v1"},{"worldUnit","metres"},{"timeUnit","seconds"},
      {"colorSpace","sRGB-display-referred"},{"pixelConvention","top-left-corner"},
      {"layout","xyz-opacity-velocity-time-inverse-sigma-factor-column-major-sh-rgb-v1"},
      {"spatialDegree",scene.sh_degree},{"temporalDegree",scene.temporal_sh_degree},
      {"timeOriginSeconds",scene.time_origin_seconds},{"focusDistance",scene.focus_distance},
      {"localTimeRange",QJsonArray{scene.local_time_range[0],scene.local_time_range[1]}},{"cameras",cameras}};
}
bool valid_point(const GaussianSceneData& scene, size_t i) {
  const auto& p=scene.points[i]; const auto& t=scene.temporal[i];
  for(const auto v:p.center_opacity) if(!std::isfinite(v) || std::abs(v)>1e10F) return false;
  for(const auto v:t.velocity_time) if(!std::isfinite(v) || std::abs(v)>1e6F) return false;
  if(p.center_opacity[3]<0 || p.center_opacity[3]>1 || !std::isfinite(t.normalization[0]) || t.normalization[0]<=0 ||
      !valid_gaussian_temporal_factor(t)) return false;
  for(const auto v:p.sh) if(!std::isfinite(v) || std::abs(v)>1e6F) return false;
  if(scene.temporal_sh_degree>0) for(const auto v:scene.temporal_appearance[i].sh)
    if(!std::isfinite(v) || std::abs(v)>1e6F) return false;
  return true;
}
constexpr size_t coefficients(const GaussianSceneData&) { return 48; }
int stored_temporal_orders(const GaussianSceneData& scene) { return scene.temporal_sh_degree>0 ? 2 : 0; }
size_t stride(const GaussianSceneData& scene) { return 18+coefficients(scene)*static_cast<size_t>(stored_temporal_orders(scene)+1); }
void spatial_covariance(GaussianPoint* point, const GaussianTemporalPoint& temporal) {
  double cov[3][3]{};
  for(size_t r=0;r<3;++r) for(size_t c=0;c<3;++c) for(size_t k=0;k<3;++k)
    cov[r][c]+=double(temporal.covariance_factor[k*4+r])*temporal.covariance_factor[k*4+c];
  point->covariance_a={static_cast<float>(cov[0][0]),static_cast<float>(cov[0][1]),static_cast<float>(cov[0][2]),static_cast<float>(cov[1][1])};
  point->covariance_b={static_cast<float>(cov[1][2]),static_cast<float>(cov[2][2]),0,0};
}
}

GaussianSceneLoad load_gaussian_spacetime_artifact(const QString& path, const QString& expected_sha256,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto fail=[](const QString& error) { return GaussianSceneLoad{{},error}; };
  if(stopped(canceled)) return fail("Temporal artifact loading canceled.");
  if(expected_sha256.size()!=64 || !std::all_of(expected_sha256.begin(),expected_sha256.end(),[](QChar c) {
      return (c>='0' && c<='9') || (c>='a' && c<='f'); })) return fail("A pinned SHA-256 is required for temporal scene loading.");
  QFile file(path);
  if(QFileInfo(path).isSymLink() || !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) ||
      file.size()<10 || file.size()>2LL*1024*1024*1024) return fail("Cannot read a bounded regular temporal artifact.");
  const auto prefix=file.read(8);
  if(prefix.size()!=8) return fail("Truncated temporal artifact.");
  const auto header_size=qFromLittleEndian<quint64>(prefix.constData());
  if(header_size<2 || header_size>static_cast<quint64>(max_header) || header_size>static_cast<quint64>(file.size()-8))
    return fail("Invalid SafeTensors header length.");
  const auto header_bytes=file.read(static_cast<qint64>(header_size));
  const auto header=object(header_bytes);
  if(!header || header->size()!=2 || !header->contains("__metadata__") || !header->contains("gaussians"))
    return fail("Unsupported or ambiguous temporal SafeTensors header.");
  const auto meta=header->value("__metadata__").toObject();
  if(meta.size()!=1 || !meta.value("framehouse.scene").isString()) return fail("Missing explicit temporal scene metadata.");
  const auto description=object(meta.value("framehouse.scene").toString().toUtf8());
  auto scene=std::make_shared<GaussianSceneData>();
  if(!description || !metadata(*description,scene.get())) return fail("Invalid temporal units, appearance, time range or camera calibration.");
  const auto tensor=header->value("gaussians").toObject();
  const auto shape=tensor.value("shape").toArray(), offsets=tensor.value("data_offsets").toArray();
  if(tensor.size()!=3 || tensor.value("dtype")!="F32" || shape.size()!=2 || offsets.size()!=2 ||
      !number(shape[0],1,double(max_points)) || shape[0].toDouble()!=std::floor(shape[0].toDouble()) ||
      !number(shape[1],double(stride(*scene)),double(stride(*scene))) ||
      !number(offsets[0],0,0)) return fail("Invalid temporal tensor type, shape or start offset.");
  const auto count=static_cast<size_t>(shape[0].toDouble());
  const auto row_bytes=stride(*scene)*4;
  const auto payload_bytes=static_cast<qint64>(count*row_bytes);
  if(!number(offsets[1],double(payload_bytes),double(payload_bytes)) || file.size()!=8+static_cast<qint64>(header_size)+payload_bytes)
    return fail("Temporal payload is truncated, oversized or contains trailing data.");
  scene->points.resize(count); scene->temporal.resize(count);
  if(scene->temporal_sh_degree>0) scene->temporal_appearance.resize(count);
  QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(prefix); hash.addData(header_bytes);
  for(size_t first=0;first<count;first+=4096) {
    if(stopped(canceled)) return fail("Temporal artifact loading canceled.");
    const auto rows=std::min(size_t{4096},count-first);
    const auto bytes=file.read(static_cast<qint64>(rows*row_bytes));
    if(bytes.size()!=static_cast<qint64>(rows*row_bytes)) return fail("Temporal artifact changed while loading.");
    hash.addData(bytes);
    for(size_t row=0;row<rows;++row) {
      size_t column=0;
      const auto next=[&] { return std::bit_cast<float>(qFromLittleEndian<quint32>(bytes.constData()+row*row_bytes+(column++)*4)); };
      auto& point=scene->points[first+row]; auto& motion=scene->temporal[first+row];
      for(auto& v:point.center_opacity) v=next();
      for(auto& v:motion.velocity_time) v=next();
      motion.normalization[0]=next();
      for(size_t c=0;c<3;++c) for(size_t r=0;r<3;++r) motion.covariance_factor[c*4+r]=next();
      for(size_t i=0;i<coefficients(*scene);++i) point.sh[i]=next();
      for(int order=0;order<stored_temporal_orders(*scene);++order) for(size_t i=0;i<coefficients(*scene);++i)
        scene->temporal_appearance[first+row].sh[static_cast<size_t>(order)*48+i]=next();
      if(!valid_point(*scene,first+row)) return fail("Invalid temporal Gaussian parameters; no scene was published.");
      spatial_covariance(&point,motion);
    }
  }
  if(stopped(canceled)) return fail("Temporal artifact loading canceled.");
  if(!file.atEnd() || QString::fromLatin1(hash.result().toHex())!=expected_sha256)
    return fail("Temporal artifact bytes do not match the pinned SHA-256.");
  scene->artifact_sha256=expected_sha256;
  return {scene,{}};
}

QVariantMap validate_gaussian_spacetime_artifact(const QString& path, const QString& expected_sha256,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  try {
    const auto loaded=load_gaussian_spacetime_artifact(path,expected_sha256,canceled);
    if(!loaded.scene) return {{"ok",false},{"error",loaded.error}};
    return {{"ok",true},{"artifactPath",path},{"artifactSha256",loaded.scene->artifact_sha256},
        {"pointCount",static_cast<qulonglong>(loaded.scene->points.size())},
        {"timeOriginSeconds",loaded.scene->time_origin_seconds},
        {"localTimeRange",QVariantList{loaded.scene->local_time_range[0],loaded.scene->local_time_range[1]}},
        {"state","validated-artifact-not-training-proof"}};
  } catch(const std::exception& error) {
    return {{"ok",false},{"error",QStringLiteral("Temporal artifact validation failed: %1").arg(QString::fromUtf8(error.what()).left(512))}};
  } catch(...) { return {{"ok",false},{"error","Temporal artifact validation failed unexpectedly."}}; }
}

QVariantMap write_gaussian_spacetime_artifact(const GaussianSceneData& scene, const QString& new_path,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto fail=[](const QString& error) { return QVariantMap{{"ok",false},{"error",error}}; };
  GaussianSceneData checked;
  if(scene.points.empty() || scene.points.size()>max_points || scene.temporal.size()!=scene.points.size() ||
      !metadata(metadata(scene),&checked) || (scene.temporal_sh_degree>0 && scene.temporal_appearance.size()!=scene.points.size()) ||
      (scene.temporal_sh_degree==0 && !scene.temporal_appearance.empty())) return fail("Invalid temporal scene for export.");
  for(size_t i=0;i<scene.points.size();++i) {
    if(stopped(canceled)) return fail("Temporal artifact export canceled.");
    if(!valid_point(scene,i)) return fail("Invalid temporal point for export.");
  }
  const auto row_bytes=stride(scene)*4;
  const QJsonObject tensor{{"dtype","F32"},{"shape",QJsonArray{double(scene.points.size()),double(stride(scene))}},
      {"data_offsets",QJsonArray{0,double(scene.points.size()*row_bytes)}}};
  const QJsonObject header{{"__metadata__",QJsonObject{{"framehouse.scene",QString::fromUtf8(QJsonDocument(metadata(scene)).toJson(QJsonDocument::Compact))}}},
      {"gaussians",tensor}};
  auto header_bytes=QJsonDocument(header).toJson(QJsonDocument::Compact);
  while(header_bytes.size()%8!=0) header_bytes.append(' ');
  if(header_bytes.size()>max_header || !QFileInfo(new_path).isAbsolute() || QFileInfo(new_path).isSymLink())
    return fail("Choose a new absolute artifact path; metadata is limited to 1 MiB.");
  QByteArray prefix(8,'\0'); qToLittleEndian<quint64>(static_cast<quint64>(header_bytes.size()),prefix.data());
  if(stopped(canceled)) return fail("Temporal artifact export canceled.");
  QFile file(new_path);
  if(!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail("Cannot create a new artifact; existing files are never overwritten.");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto write=[&](const QByteArray& bytes) { hash.addData(bytes); return file.write(bytes)==bytes.size(); };
  if(!write(prefix) || !write(header_bytes)) return fail("Cannot write artifact header; partial new file retained.");
  for(size_t first=0;first<scene.points.size();first+=4096) {
    if(stopped(canceled)) return fail("Temporal artifact export canceled; partial new file retained.");
    const auto rows=std::min(size_t{4096},scene.points.size()-first);
    QByteArray bytes(static_cast<qsizetype>(rows*row_bytes),'\0');
    for(size_t row=0;row<rows;++row) {
      size_t column=0;
      const auto put=[&](float value) { qToLittleEndian<quint32>(std::bit_cast<quint32>(value),bytes.data()+row*row_bytes+(column++)*4); };
      const auto& point=scene.points[first+row]; const auto& motion=scene.temporal[first+row];
      for(const auto v:point.center_opacity) put(v);
      for(const auto v:motion.velocity_time) put(v);
      put(motion.normalization[0]);
      for(size_t c=0;c<3;++c) for(size_t r=0;r<3;++r) put(motion.covariance_factor[c*4+r]);
      for(size_t i=0;i<coefficients(scene);++i) put(point.sh[i]);
      for(int order=0;order<stored_temporal_orders(scene);++order) for(size_t i=0;i<coefficients(scene);++i)
        put(scene.temporal_appearance[first+row].sh[static_cast<size_t>(order)*48+i]);
    }
    if(!write(bytes)) return fail("Cannot write temporal payload; partial new file retained.");
  }
  if(!file.flush() || stopped(canceled)) return fail("Temporal artifact export did not finish; new file retained.");
  return {{"ok",true},{"artifactPath",new_path},{"artifactSha256",QString::fromLatin1(hash.result().toHex())},
      {"pointCount",static_cast<qulonglong>(scene.points.size())},{"state","artifact-written-not-training-proof"}};
}
}
