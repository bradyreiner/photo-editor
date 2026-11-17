#pragma once
#include "qimage_color_conversion.h"
#include "gaussian_scene_mesh.h"
#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <optional>
#include <mutex>

namespace photo_editor::desktop {
struct GaussianSurfaceMaterial {
  QString name, texture_sha256;
  QVector3D color{1,1,1}, emission{}; // Linear reflectance and emitted RGB radiance.
  QVector2D uv_scale{1,1}, uv_offset{};
  float roughness = 0.35F, metallic = 0;
  bool use_texture = true, clamp = false;
  QVariantMap description() const {
    return {{"name",name},{"color",QVariantList{color.x(),color.y(),color.z()}},{"emission",QVariantList{emission.x(),emission.y(),emission.z()}},
        {"roughness",roughness},{"metallic",metallic},{"textureSha256",texture_sha256},{"useTexture",use_texture},{"clamp",clamp},
        {"uvScale",QVariantList{uv_scale.x(),uv_scale.y()}},{"uvOffset",QVariantList{uv_offset.x(),uv_offset.y()}}};
  }
};
inline std::optional<GaussianSurfaceMaterial> gaussian_surface_material(const QVariantMap& value) {
  const auto o = QJsonObject::fromVariantMap(value); GaussianSurfaceMaterial result;
  if (o.size() != 10 || !o.value("name").isString() || !o.value("textureSha256").isString() || !o.value("useTexture").isBool() || !o.value("clamp").isBool()) return {};
  result.name = o.value("name").toString(); result.texture_sha256 = o.value("textureSha256").toString();
  if (result.name.isEmpty() || result.name.size() > 80 || (!result.texture_sha256.isEmpty() && !valid_gaussian_mesh_hash(result.texture_sha256))) return {};
  const auto scalar = [&](const char* key,float low,float high,float& out) {
    const auto v = o.value(QString::fromLatin1(key)); if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
    out = static_cast<float>(v.toDouble()); return std::isfinite(out) && out >= low && out <= high;
  };
  const auto vector = [&](const char* key,int count,float low,float high,float* out) {
    const auto v = o.value(QString::fromLatin1(key)); if (!v.isArray() || v.toArray().size() != count) return false;
    const auto a = v.toArray();
    for (int i = 0; i < count; ++i) { if (!a[i].isDouble() || !std::isfinite(a[i].toDouble())) return false; out[i] = static_cast<float>(a[i].toDouble()); if (!std::isfinite(out[i]) || out[i] < low || out[i] > high) return false; }
    return true;
  };
  float color[3], emission[3], scale[2], offset[2];
  if (!scalar("roughness",0.04F,1,result.roughness) || !scalar("metallic",0,1,result.metallic) || !vector("color",3,0,1,color) ||
      !vector("emission",3,0,10000,emission) || !vector("uvScale",2,-1e6F,1e6F,scale) || !vector("uvOffset",2,-1e6F,1e6F,offset)) return {};
  result.color = {color[0],color[1],color[2]}; result.emission = {emission[0],emission[1],emission[2]};
  result.uv_scale = {scale[0],scale[1]}; result.uv_offset = {offset[0],offset[1]};
  result.use_texture = o.value("useTexture").toBool(); result.clamp = o.value("clamp").toBool(); return result;
}
struct GaussianColorTexture { QString sha256; QImage image; };
struct GaussianMaterialAssets {
  QString sha256, mesh_sha256;
  std::vector<GaussianSurfaceMaterial> materials;
  QMap<QString,std::shared_ptr<const GaussianColorTexture>> textures;
  QStringList warnings;
};
struct GaussianMaterialLoad {
  std::shared_ptr<const GaussianMaterialAssets> assets;
  QString error;
  QByteArray manifest;
  QMap<QString,QByteArray> texture_sources;
};
inline GaussianMaterialLoad gaussian_material_error(QString message) { return {{},std::move(message),{},{}}; }
inline QByteArray gaussian_material_manifest(const GaussianMaterialAssets& assets) {
  QVariantList materials; for (const auto& material : assets.materials) materials.append(material.description());
  return QJsonDocument::fromVariant(QVariantMap{{"schema","looksy-mesh-materials-v1"},{"meshSha256",assets.mesh_sha256},{"materials",materials},{"warnings",assets.warnings}}).toJson(QJsonDocument::Compact);
}
inline std::shared_ptr<const GaussianColorTexture> decode_gaussian_color_texture(const QByteArray& bytes) {
  if (bytes.isEmpty() || bytes.size() > 64*1024*1024) return {};
  const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
  static std::mutex mutex; static QMap<QString,std::weak_ptr<const GaussianColorTexture>> cache;
  { std::lock_guard lock(mutex); const auto found = cache.value(hash).lock(); if (found) return found; }
  QBuffer buffer; buffer.setData(bytes); if (!buffer.open(QIODevice::ReadOnly)) return {};
  QImageReader reader(&buffer); reader.setAutoTransform(false);
  const auto size = reader.size(); if (size.isEmpty() || size.width() > 8192 || size.height() > 8192) return {};
  auto image = reader.read(); if (image.isNull()) return {};
  if (image.colorSpace().isValid()) image = convert_tagged_qimage_color_space(image, QColorSpace(QColorSpace::SRgb), QImageColorConversionPurpose::DecodeNormalization);
  image = image.convertToFormat(QImage::Format_RGBA8888); image.setColorSpace(QColorSpace::SRgb);
  if (image.isNull()) return {};
  auto texture = std::make_shared<GaussianColorTexture>(GaussianColorTexture{hash,std::move(image)});
  { std::lock_guard lock(mutex);
    const auto found = cache.value(hash).lock(); if (found) return found;
    if (cache.size() > 128) for (auto it = cache.begin(); it != cache.end();) { if (it.value().expired()) it = cache.erase(it); else ++it; }
    cache.insert(hash,texture);
  }
  return texture;
}
inline GaussianMaterialLoad load_gaussian_obj_materials(const QString& obj_path,const GaussianSceneMesh& mesh,const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  if (mesh.material_libraries.isEmpty() && mesh.material_names.size() <= 1) return {};
  auto assets = std::make_shared<GaussianMaterialAssets>(); assets->mesh_sha256 = mesh.sha256;
  QMap<QString,GaussianSurfaceMaterial> definitions; QMap<QString,QString> texture_paths;
  QMap<QString,QByteArray> texture_sources;
  const auto obj_root = QFileInfo(obj_path).absoluteDir().canonicalPath();
  const auto resolve = [&](const QString& root,QString reference) {
    reference = reference.trimmed();
    if (reference.size() >= 2 && ((reference.front() == QLatin1Char('"') && reference.back() == QLatin1Char('"')) || (reference.front() == QLatin1Char('\'') && reference.back() == QLatin1Char('\'')))) reference = reference.mid(1,reference.size()-2);
    reference.replace('\\','/');
    const auto path = QFileInfo(QDir(root).filePath(reference)).canonicalFilePath();
    // Material dependencies are files in the selected OBJ's directory tree.
    return !QFileInfo(reference).isAbsolute() && !path.isEmpty() && path.startsWith(obj_root+"/") ? path : QString();
  };
  for (const auto& library : mesh.material_libraries) {
    if (canceled && canceled->load()) return gaussian_material_error("Material import canceled.");
    const auto path = resolve(obj_root,library); QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 4*1024*1024) return gaussian_material_error("Cannot read a referenced MTL inside the OBJ directory. Import geometry only or place its material files beside the OBJ.");
    GaussianSurfaceMaterial material; bool active = false, explicit_roughness = false;
    const auto finish = [&] { if (active) definitions.insert(material.name,material); };
    int line_number = 0;
    while (!file.atEnd()) {
      if (canceled && canceled->load()) return gaussian_material_error("Material import canceled.");
      auto line = file.readLine(65538); ++line_number;
      if (line.size() > 65536) return gaussian_material_error("An MTL line exceeds 64 KiB.");
      const auto comment = line.indexOf('#'); if (comment >= 0) line.truncate(comment);
      line = line.simplified(); const auto tokens = line.split(' '); if (line.isEmpty()) continue;
      const auto error = [&](const QString& reason) { return gaussian_material_error(QStringLiteral("MTL %1 line %2: %3").arg(QFileInfo(path).fileName()).arg(line_number).arg(reason)); };
      const auto tag = tokens[0];
      if (tag == "newmtl") {
        finish(); material = {}; material.name = QString::fromUtf8(line.mid(7)); active = true; explicit_roughness = false;
        texture_paths.remove(material.name);
        if (material.name.isEmpty() || material.name.size() > 80 || definitions.size() >= 256) return error("Invalid or excessive material definitions.");
        continue;
      }
      if (!active) return error("A material property precedes newmtl.");
      const auto number = [&](qsizetype i,float low,float high,float& out) {
        bool ok = false; if (i >= tokens.size()) return false; out = tokens[i].toFloat(&ok); return ok && std::isfinite(out) && out >= low && out <= high;
      };
      if (tag == "Kd" || tag == "Ke") {
        float xyz[3]; if (tokens.size() != 4) return error("Expected RGB material coefficients.");
        for (int i = 0; i < 3; ++i) if (!number(i+1,0,tag == "Ke" ? 10000.0F : 1.0F,xyz[i])) return error("Invalid RGB material coefficients.");
        if (tag == "Kd") material.color = {xyz[0],xyz[1],xyz[2]}; else material.emission = {xyz[0],xyz[1],xyz[2]};
      } else if (tag == "Pr" || tag == "Pm" || tag == "Ns") {
        float value = 0; if (tokens.size() != 2 || !number(1,0,tag == "Ns" ? 1000.0F : 1.0F,value)) return error("Invalid roughness, metallic or exponent.");
        if (tag == "Pr") { material.roughness = std::max(0.04F,value); explicit_roughness = true; }
        else if (tag == "Pm") material.metallic = value;
        else if (!explicit_roughness) material.roughness = std::clamp(1.0F-std::sqrt(value/1000.0F),0.04F,1.0F);
      } else if (tag == "map_Kd") {
        qsizetype i = 1;
        while (i < tokens.size() && tokens[i].startsWith('-')) {
          const auto option = tokens[i++];
          if (option == "-clamp") { if (i >= tokens.size() || (tokens[i] != "on" && tokens[i] != "off")) return error("Invalid map clamp option."); material.clamp = tokens[i++] == "on"; }
          else if (option == "-s" || option == "-o") {
            float values[3]{option == "-s" ? 1.0F : 0.0F,option == "-s" ? 1.0F : 0.0F,option == "-s" ? 1.0F : 0.0F}; int count = 0;
            while (count < 3 && i < tokens.size()) { float value; if (!number(i,-1e6F,1e6F,value)) break; values[count++] = value; ++i; }
            if (count == 0) return error("Invalid map scale/offset.");
            if (option == "-s") material.uv_scale = {values[0],values[1]}; else material.uv_offset = {values[0],values[1]};
          } else return error("Unsupported color-map option: "+QString::fromUtf8(option));
        }
        if (i >= tokens.size()) return error("Missing color image.");
        QStringList parts; for (; i < tokens.size(); ++i) parts.append(QString::fromUtf8(tokens[i]));
        const auto texture_path = resolve(QFileInfo(path).absolutePath(),parts.join(' '));
        if (texture_path.isEmpty()) return error("Color image must be inside the OBJ directory tree.");
        texture_paths.insert(material.name,texture_path);
      } else if (tag == "d" || tag == "Tr") {
        float value = 0; if (tokens.size() != 2 || !number(1,0,1,value)) return error("Invalid material opacity.");
        if ((tag == "d" && value < 1) || (tag == "Tr" && value > 0)) assets->warnings.append("Transparent material surfaces are rendered opaque.");
      } else if (tag.startsWith("map_") || tag == "bump" || tag == "disp" || tag == "norm") assets->warnings.append("Only base-color images are applied; other material maps are ignored.");
      // Ka, Ks, Ni, illum and exporter-specific lobes are not GGX inputs.
    }
    finish();
  }
  qint64 decoded_bytes = 0, source_bytes = 0;
  for (const auto& name : mesh.material_names) {
    GaussianSurfaceMaterial material; material.name = name.isEmpty() ? QStringLiteral("Default") : name;
    if (!name.isEmpty()) {
      if (!definitions.contains(name)) return gaussian_material_error("A face material has no MTL definition: "+name+". Include its MTL or import geometry only.");
      material = definitions.value(name);
    }
    if (texture_paths.contains(name)) {
      QFile file(texture_paths.value(name));
      if (!file.open(QIODevice::ReadOnly) || file.size() > 64*1024*1024) return gaussian_material_error("Cannot read a color image up to 64 MiB.");
      const auto bytes = file.readAll(); const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
      material.texture_sha256 = hash;
      if (!assets->textures.contains(hash)) {
        const auto texture = decode_gaussian_color_texture(bytes);
        if (!texture) return gaussian_material_error("Cannot decode a color image up to 8192 pixels per edge.");
        decoded_bytes += texture->image.sizeInBytes(); source_bytes += bytes.size();
        if (assets->textures.size() >= 32 || decoded_bytes > 256*1024*1024 || source_bytes > 128*1024*1024) return gaussian_material_error("Imported color images exceed 32 images, 128 MiB of source data or 256 MiB of decoded pixels.");
        assets->textures.insert(hash,texture); texture_sources.insert(hash,bytes);
        bool alpha = false;
        for (int y = 0; y < texture->image.height() && !alpha; ++y) {
          const auto* row = texture->image.constScanLine(y); for (int x = 0; x < texture->image.width(); ++x) if (row[x*4+3] != 255) { alpha = true; break; }
        }
        if (alpha) assets->warnings.append("Color-image alpha is ignored; textured surfaces are opaque.");
      }
    }
    assets->materials.push_back(material);
  }
  for (const auto& t : mesh.triangles) if (t.uv2_material[3] == 0 && !assets->materials[static_cast<size_t>(t.uv2_material[2])].texture_sha256.isEmpty())
    return gaussian_material_error("A textured face has missing UV coordinates. Export complete UVs or import geometry only.");
  assets->warnings.removeDuplicates(); const auto manifest = gaussian_material_manifest(*assets);
  assets->sha256 = QString::fromLatin1(QCryptographicHash::hash(manifest,QCryptographicHash::Sha256).toHex());
  return {assets,{},manifest,texture_sources};
}
inline QString gaussian_material_asset_path(const QString& project,const QString& hash) { return QDir(project).filePath("materials/"+hash+".json"); }
inline QString gaussian_texture_asset_path(const QString& project,const QString& hash) { return QDir(project).filePath("materials/textures/"+hash+".image"); }
inline QString store_gaussian_material_assets(const QString& project,const GaussianMaterialLoad& loaded) {
  if (!loaded.assets || !valid_gaussian_mesh_hash(loaded.assets->sha256)) return "No imported materials to store.";
  const auto directory = QDir(project).filePath("materials"), textures = QDir(directory).filePath("textures");
  if (QFileInfo(directory).isSymLink() || QFileInfo(textures).isSymLink() || !QDir().mkpath(textures)) return "Cannot create project material storage.";
  const auto publish = [](const QString& path,const QByteArray& bytes) -> QString {
    if (QFileInfo(path).isSymLink()) return "Material assets must remain inside the project.";
    if (QFileInfo::exists(path)) { QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() != bytes.size() || file.readAll() != bytes) return "Existing material asset failed its integrity check."; return {}; }
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return "Cannot store project material assets."; return {};
  };
  for (auto it = loaded.texture_sources.cbegin(); it != loaded.texture_sources.cend(); ++it) {
    const auto error = publish(gaussian_texture_asset_path(project,it.key()),it.value()); if (!error.isEmpty()) return error;
  }
  return publish(gaussian_material_asset_path(project,loaded.assets->sha256),loaded.manifest);
}
inline GaussianMaterialLoad load_gaussian_material_assets(const QString& project,const QString& hash,const QString& mesh_hash,const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  if (!valid_gaussian_mesh_hash(hash) || QFileInfo(QDir(project).filePath("materials")).isSymLink() || QFileInfo(QDir(project).filePath("materials/textures")).isSymLink()) return gaussian_material_error("Invalid project material reference.");
  const auto path = gaussian_material_asset_path(project,hash); QFile file(path);
  if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 128*1024) return gaussian_material_error("Cannot read project materials.");
  const auto bytes = file.readAll();
  if (QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()) != hash) return gaussian_material_error("Project materials failed their integrity check.");
  const auto root = QJsonDocument::fromJson(bytes).object();
  if (root.size() != 4 || root.value("schema") != "looksy-mesh-materials-v1" || root.value("meshSha256") != mesh_hash ||
      !root.value("materials").isArray() || !root.value("warnings").isArray() || root.value("materials").toArray().isEmpty() || root.value("materials").toArray().size() > 64) return gaussian_material_error("Invalid material manifest.");
  auto assets = std::make_shared<GaussianMaterialAssets>(); assets->sha256 = hash; assets->mesh_sha256 = mesh_hash; qint64 decoded_bytes = 0;
  for (const auto& entry : root.value("warnings").toArray()) { if (!entry.isString() || entry.toString().size() > 256 || assets->warnings.size() >= 64) return gaussian_material_error("Invalid material notices."); assets->warnings.append(entry.toString()); }
  for (const auto& entry : root.value("materials").toArray()) {
    if (canceled && canceled->load()) return gaussian_material_error("Material loading canceled.");
    const auto material = gaussian_surface_material(entry.toObject().toVariantMap()); if (!material) return gaussian_material_error("Invalid imported material.");
    assets->materials.push_back(*material);
    if (material->texture_sha256.isEmpty() || assets->textures.contains(material->texture_sha256)) continue;
    const auto image_path = gaussian_texture_asset_path(project,material->texture_sha256); QFile image_file(image_path);
    if (QFileInfo(image_path).isSymLink() || !image_file.open(QIODevice::ReadOnly) || image_file.size() > 64*1024*1024) return gaussian_material_error("Cannot read project color image.");
    const auto image_bytes = image_file.readAll();
    if (QString::fromLatin1(QCryptographicHash::hash(image_bytes,QCryptographicHash::Sha256).toHex()) != material->texture_sha256) return gaussian_material_error("Project color image failed its integrity check.");
    const auto texture = decode_gaussian_color_texture(image_bytes); if (!texture) return gaussian_material_error("Cannot decode project color image.");
    decoded_bytes += texture->image.sizeInBytes();
    if (assets->textures.size() >= 32 || decoded_bytes > 256*1024*1024) return gaussian_material_error("Project color images exceed their memory limit.");
    assets->textures.insert(material->texture_sha256,texture);
  }
  return {assets,{},{},{}};
}
} // namespace photo_editor::desktop
