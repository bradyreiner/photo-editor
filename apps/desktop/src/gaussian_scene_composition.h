#pragma once
#include "gaussian_alpha_model.h"
#include "gaussian_scene_materials.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QLockFile>
#include <QQuaternion>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>
#include <QVector3D>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace photo_editor::desktop {
struct GaussianSceneObject {
  QString id, name, mesh_sha256;
  std::shared_ptr<const GaussianSceneMesh> mesh;
  QString material_set_sha256;
  std::shared_ptr<const GaussianMaterialAssets> material_assets;
  std::vector<GaussianSurfaceMaterial> materials;
  bool use_imported_materials = true;
  int kind = 0;  // Analytic unit sphere or box with unit half extents.
  QVector3D position{0, 0, 3}, size{0.5F, 0.5F, 0.5F}, rotation{};
  QVector3D color{0.65F, 0.65F, 0.65F};  // Display sRGB material input.
  float roughness = 0.35F, metallic = 0, emission = 0;
  bool visible = true;
  QVariantMap description() const {
    const auto xyz = [](QVector3D v) { return QVariantList{v.x(), v.y(), v.z()}; };
    QVariantMap result{{"id", id}, {"name", name}, {"kind", kind == 0 ? "sphere" : kind == 1 ? "box" : kind == 2 ? "mesh" : "invalid"},
        {"position", xyz(position)}, {"size", xyz(size)}, {"rotation", xyz(rotation)}, {"color", xyz(color)},
        {"roughness", roughness}, {"metallic", metallic}, {"emission", emission}, {"visible", visible}};
    if (kind == 2) result.insert("meshSha256",mesh_sha256);
    if (!material_set_sha256.isEmpty()) {
      QVariantList surfaces; for (const auto& material : materials) surfaces.append(material.description());
      result.insert("materialSetSha256",material_set_sha256); result.insert("useImportedMaterials",use_imported_materials); result.insert("materials",surfaces);
    }
    return result;
  }
};
struct GaussianSceneLight {
  QString id, name;
  int kind = 0;  // Directional, point, rectangle or spherical emitter.
  QVector3D vector{-0.4F, -0.7F, -0.6F}, color{1, 1, 1};
  float intensity = 3, radius = 0.25F, angular_radius = 0;
  QVector2D size{1,1}; QVector3D rotation{};
  bool soft() const { return kind >= 2 || (kind == 0 && angular_radius > 0); }
  bool visible = true;
  bool camera_visible = false;
  bool primaryVisible() const { return visible && camera_visible && (kind == 2 || kind == 3); }
  QVariantMap description() const {
    const auto xyz = [](QVector3D v) { return QVariantList{v.x(), v.y(), v.z()}; };
    QVariantMap result{{"id", id}, {"name", name}, {"kind", kind == 0 ? "directional" : kind == 1 ? "point" : kind == 2 ? "rectangle" : kind == 3 ? "sphere" : "invalid"},
        {"vector", xyz(vector)}, {"color", xyz(color)}, {"intensity", intensity}, {"visible", visible}};
    if (kind == 0 && angular_radius != 0) result.insert("angularRadius",angular_radius);
    if (kind == 2) { result.insert("size",QVariantList{size.x(),size.y()}); result.insert("rotation",xyz(rotation)); }
    if (kind == 3) result.insert("radius",radius);
    if (camera_visible) result.insert("cameraVisible",true);
    return result;
  }
};
struct GaussianSceneComposition {
  std::vector<GaussianSceneObject> objects;
  std::vector<GaussianSceneLight> lights;
  float ambient = 0.15F;
  bool reflections = false;
  bool spatial_reflections = false;
  int light_samples = 64;
  bool environment_diffuse = false;
  bool spatial_environment = false;
  int surface_bounces = 1;
  bool scan_shadows = false;
  bool capture_emission = false;
  bool capture_emission_importance = false;
  bool capture_emission_sampling_explicit = false;
  float environment_strength = 1;
  GaussianAlphaModel alpha_model = GaussianAlphaModel::Brush999;
  bool softLights() const { for (const auto& light : lights) if (light.visible && light.intensity > 0 && light.soft()) return true; return false; }
  bool environmentLighting() const { return environment_diffuse && environment_strength > 0; }
  bool hasVisibleObjects() const { for (const auto& object : objects) if (object.visible) return true; return false; }
  bool hasEmittingObjects() const {
    for (const auto& object : objects) if (object.visible) {
      if (object.use_imported_materials && !object.material_set_sha256.isEmpty()) {
        for (const auto& material : object.materials) if (material.emission.lengthSquared() > 0) return true;
      } else if (object.emission > 0 && object.color.lengthSquared() > 0) return true;
    }
    return false;
  }
  bool needsProbe() const { return hasVisibleObjects() && ((reflections && !spatial_reflections) || (environmentLighting() && !spatial_environment)); }
  bool softLighting() const { return (hasVisibleObjects() && (environmentLighting() || (reflections && spatial_reflections))) || ((hasVisibleObjects() || capture_lighting != 0) && softLights()) || (capture_emission && capture_lighting == 1 && hasEmittingObjects()); }
  int capture_lighting = 0;  // 0 captured appearance, 1 approximate relighting, 2 differential shadow receiver.
  float capture_power = 1, shadow_strength = 1;
  float haze_density = 0, haze_distance = 1000, haze_anisotropy = 0;
  bool haze_shadows = false;
  QVector3D haze_color{0.7F, 0.8F, 1.0F};
  bool changesCapturedAppearance() const { return capture_lighting != 0 || haze_density > 0; }
  bool extendedSettings() const {
    return changesCapturedAppearance() || capture_power != 1 || shadow_strength != 1 || haze_distance != 1000 ||
        haze_anisotropy != 0 || haze_color != QVector3D(0.7F,0.8F,1.0F);
  }
  QString captureLightingName() const { return capture_lighting == 0 ? "captured" : capture_lighting == 1 ? "lit-color" : capture_lighting == 2 ? "shadow-receiver" : "invalid"; }
  QVariantMap description() const {
    QVariantList items, lamps;
    for (const auto& object : objects) items.append(object.description());
    for (const auto& light : lights) lamps.append(light.description());
    bool visible_sources = false; for (const auto& light : lights) visible_sources = visible_sources || light.camera_visible;
    QVariantMap result{{"schema", "looksy-scene-composition-v1"}, {"objects", items}, {"lights", lamps}, {"ambient", ambient}};
    if (reflections) { result.insert("schema","looksy-scene-composition-v2"); result.insert("reflections",true); }
    if (extendedSettings()) {
      result.insert("schema", "looksy-scene-composition-v3"); result.insert("reflections", reflections);
      result.insert("captureLighting", QVariantMap{{"mode", captureLightingName()},
          {"power", capture_power}, {"shadowStrength", shadow_strength}});
      result.insert("haze", QVariantMap{{"density", haze_density}, {"distance", haze_distance},
          {"anisotropy", haze_anisotropy}, {"color", QVariantList{haze_color.x(),haze_color.y(),haze_color.z()}}});
    }
    for (const auto& object : objects) if (object.kind == 2) {
      result.insert("schema","looksy-scene-composition-v4"); result.insert("reflections",reflections);
      result.insert("captureLighting",QVariantMap{{"mode",captureLightingName()}, {"power",capture_power},{"shadowStrength",shadow_strength}});
      result.insert("haze",QVariantMap{{"density",haze_density},{"distance",haze_distance},{"anisotropy",haze_anisotropy},{"color",QVariantList{haze_color.x(),haze_color.y(),haze_color.z()}}});
      break;
    }
    for (const auto& object : objects) if (!object.material_set_sha256.isEmpty()) { result.insert("schema","looksy-scene-composition-v5"); break; }
    bool sampled = light_samples != 64; for (const auto& light : lights) sampled = sampled || light.soft();
    if (sampled) {
      result.insert("schema","looksy-scene-composition-v6"); result.insert("reflections",reflections); result.insert("lightSamples",light_samples);
      result.insert("captureLighting",QVariantMap{{"mode",captureLightingName()},{"power",capture_power},{"shadowStrength",shadow_strength}});
      result.insert("haze",QVariantMap{{"density",haze_density},{"distance",haze_distance},{"anisotropy",haze_anisotropy},{"color",QVariantList{haze_color.x(),haze_color.y(),haze_color.z()}}});
    }
    if (environment_diffuse || environment_strength != 1 || alpha_model != GaussianAlphaModel::Brush999 || visible_sources || scan_shadows || spatial_reflections || spatial_environment || surface_bounces != 1 || capture_emission || capture_emission_importance || capture_emission_sampling_explicit || haze_shadows || capture_lighting == 2) {
      result.insert("schema","looksy-scene-composition-v7"); result.insert("reflections",reflections); result.insert("lightSamples",light_samples);
      result.insert("captureLighting",QVariantMap{{"mode",captureLightingName()},{"power",capture_power},{"shadowStrength",shadow_strength}});
      result.insert("haze",QVariantMap{{"density",haze_density},{"distance",haze_distance},{"anisotropy",haze_anisotropy},{"color",QVariantList{haze_color.x(),haze_color.y(),haze_color.z()}}});
      result.insert("environment",QVariantMap{{"diffuse",environment_diffuse},{"strength",environment_strength}});
    }
    if (alpha_model != GaussianAlphaModel::Brush999 || visible_sources || scan_shadows || spatial_reflections || spatial_environment || surface_bounces != 1 || capture_emission || capture_emission_importance || capture_emission_sampling_explicit || haze_shadows || capture_lighting == 2) {
      result.insert("schema","looksy-scene-composition-v8"); result.insert("alphaModel",gaussian_alpha_model_name(alpha_model));
    }
    if (visible_sources) result.insert("schema","looksy-scene-composition-v9");
    if (scan_shadows) { result.insert("schema","looksy-scene-composition-v10"); result.insert("scanShadows",true); }
    if (spatial_reflections) { result.insert("schema","looksy-scene-composition-v11"); result.insert("scanShadows",scan_shadows); result.insert("reflectionMode","spatial"); }
    if (spatial_environment) {
      result.insert("schema","looksy-scene-composition-v12"); result.insert("scanShadows",scan_shadows);
      result.insert("reflectionMode",spatial_reflections ? "spatial" : "probe"); result.insert("illuminationMode","spatial");
    }
    if (surface_bounces != 1 || capture_emission || capture_emission_importance || capture_emission_sampling_explicit || haze_shadows || capture_lighting == 2) {
      result.insert("schema","looksy-scene-composition-v13"); result.insert("scanShadows",scan_shadows);
      result.insert("reflectionMode",spatial_reflections ? "spatial" : "probe");
      result.insert("illuminationMode",spatial_environment ? "spatial" : "probe"); result.insert("surfaceBounces",surface_bounces);
    }
    if (capture_emission || capture_emission_importance || capture_emission_sampling_explicit || haze_shadows || capture_lighting == 2) { result.insert("schema","looksy-scene-composition-v14"); result.insert("captureEmission",capture_emission); }
    if (capture_emission_importance || capture_emission_sampling_explicit || haze_shadows || capture_lighting == 2) { result.insert("schema","looksy-scene-composition-v15"); result.insert("captureEmissionSampling",capture_emission_importance ? "bounds-mixture" : "cosine"); }
    if (haze_shadows || capture_lighting == 2) { result.insert("schema","looksy-scene-composition-v16"); result.insert("hazeShadows",haze_shadows); }
    if (capture_lighting == 2) result.insert("schema","looksy-scene-composition-v17");
    return result;
  }
  bool active() const {
    if (hasVisibleObjects()) return true;
    for (const auto& light : lights) if (light.primaryVisible()) return true;
    return false;
  }
};
inline std::optional<GaussianSceneComposition> gaussian_scene_composition(const QVariantMap& value) {
  const auto json = QJsonObject::fromVariantMap(value);
  const bool shadow_receiver = json.value("schema") == "looksy-scene-composition-v17";
  const bool haze_shadows = shadow_receiver || json.value("schema") == "looksy-scene-composition-v16";
  const bool importance = haze_shadows || json.value("schema") == "looksy-scene-composition-v15";
  const bool capture_emission = importance || json.value("schema") == "looksy-scene-composition-v14";
  const bool surface_paths = capture_emission || json.value("schema") == "looksy-scene-composition-v13";
  const bool spatial_environment = surface_paths || json.value("schema") == "looksy-scene-composition-v12";
  const bool spatial = spatial_environment || json.value("schema") == "looksy-scene-composition-v11";
  const bool scan_shadows = spatial || json.value("schema") == "looksy-scene-composition-v10";
  const bool visible_sources = scan_shadows || json.value("schema") == "looksy-scene-composition-v9";
  const bool alpha = visible_sources || json.value("schema") == "looksy-scene-composition-v8";
  const bool environment = alpha || json.value("schema") == "looksy-scene-composition-v7";
  const bool sampled = environment || json.value("schema") == "looksy-scene-composition-v6";
  const bool textured = sampled || json.value("schema") == "looksy-scene-composition-v5";
  const bool geometry = textured || json.value("schema") == "looksy-scene-composition-v4";
  const bool extended = geometry || json.value("schema") == "looksy-scene-composition-v3";
  const bool reflective = extended || json.value("schema") == "looksy-scene-composition-v2";
  if (json.size() != (haze_shadows ? 17 : importance ? 16 : capture_emission ? 15 : surface_paths ? 14 : spatial_environment ? 13 : spatial ? 12 : scan_shadows ? 11 : alpha ? 10 : environment ? 9 : sampled ? 8 : extended ? 7 : reflective ? 5 : 4) || (!reflective && json.value("schema") != "looksy-scene-composition-v1") ||
      (reflective && !json.value("reflections").isBool()) ||
      !json.value("objects").isArray() || !json.value("lights").isArray() || !json.value("ambient").isDouble()) return {};
  const auto objects = json.value("objects").toArray(), lights = json.value("lights").toArray();
  if (objects.size() > 16 || lights.size() > 8) return {};
  GaussianSceneComposition result;
  if (haze_shadows) {
    if (!json.value("hazeShadows").isBool()) return {};
    result.haze_shadows = json.value("hazeShadows").toBool();
  }
  if (importance) {
    if (json.value("captureEmissionSampling") != "bounds-mixture" && json.value("captureEmissionSampling") != "cosine") return {};
    result.capture_emission_importance = json.value("captureEmissionSampling") == "bounds-mixture";
    result.capture_emission_sampling_explicit = true;
  }
  if (capture_emission) {
    if (!json.value("captureEmission").isBool()) return {};
    result.capture_emission = json.value("captureEmission").toBool();
  }
  if (surface_paths) {
    const auto depth = json.value("surfaceBounces");
    if (!depth.isDouble() || !std::isfinite(depth.toDouble()) || depth.toDouble() < 1 || depth.toDouble() > 8 || depth.toDouble() != std::floor(depth.toDouble())) return {};
    result.surface_bounces = depth.toInt();
  }
  if (spatial_environment) {
    if (json.value("illuminationMode") != "probe" && json.value("illuminationMode") != "spatial") return {};
    result.spatial_environment = json.value("illuminationMode") == "spatial";
  }
  if (spatial) {
    if (json.value("reflectionMode") != "probe" && json.value("reflectionMode") != "spatial") return {};
    result.spatial_reflections = json.value("reflectionMode") == "spatial";
  }
  if (scan_shadows) {
    if (!json.value("scanShadows").isBool()) return {};
    result.scan_shadows = json.value("scanShadows").toBool();
  }
  result.reflections = reflective && json.value("reflections").toBool();
  if (alpha) {
    if (!json.value("alphaModel").isString()) return {};
    const auto model = parse_gaussian_alpha_model(json.value("alphaModel").toString());
    if (!model) return {};
    result.alpha_model = *model;
  }
  if (environment) {
    if (!json.value("environment").isObject()) return {};
    const auto settings = json.value("environment").toObject();
    const auto strength = settings.value("strength").toDouble(-1);
    if (settings.size() != 2 || !settings.value("diffuse").isBool() || !settings.value("strength").isDouble() ||
        !std::isfinite(strength) || strength < 0 || strength > 10) return {};
    result.environment_diffuse = settings.value("diffuse").toBool(); result.environment_strength = static_cast<float>(strength);
  }
  if (sampled) {
    const auto count = json.value("lightSamples");
    if (!count.isDouble() || (count.toDouble() != 16 && count.toDouble() != 32 && count.toDouble() != 64 && count.toDouble() != 128 && count.toDouble() != 256 && count.toDouble() != 512 && count.toDouble() != 1024)) return {};
    result.light_samples = count.toInt();
  }
  const auto scalar = [](const QJsonObject& o, const char* key, double low, double high, float& out) {
    const auto v = o.value(QString::fromLatin1(key));
    if (!v.isDouble() || !std::isfinite(v.toDouble())) return false;
    out = static_cast<float>(v.toDouble());
    return std::isfinite(out) && out >= static_cast<float>(low) && out <= static_cast<float>(high);
  };
  const auto vector = [](const QJsonObject& o, const char* key, double low, double high, QVector3D& out) {
    const auto v = o.value(QString::fromLatin1(key));
    if (!v.isArray() || v.toArray().size() != 3) return false;
    const auto a = v.toArray(); float xyz[3]{};
    for (int i = 0; i < 3; ++i) {
      if (!a[i].isDouble() || !std::isfinite(a[i].toDouble())) return false;
      xyz[i] = static_cast<float>(a[i].toDouble());
      if (!std::isfinite(xyz[i]) || xyz[i] < static_cast<float>(low) || xyz[i] > static_cast<float>(high)) return false;
    }
    out = {xyz[0], xyz[1], xyz[2]}; return true;
  };
  if (extended) {
    if (!json.value("captureLighting").isObject() || !json.value("haze").isObject()) return {};
    const auto capture = json.value("captureLighting").toObject(), haze = json.value("haze").toObject();
    if (capture.size() != 3 || haze.size() != 4 ||
        (capture.value("mode") != "captured" && capture.value("mode") != "lit-color" && (!shadow_receiver || capture.value("mode") != "shadow-receiver")) ||
        !scalar(capture,"power",0,100,result.capture_power) || !scalar(capture,"shadowStrength",0,1,result.shadow_strength) ||
        !scalar(haze,"density",0,100,result.haze_density) || !scalar(haze,"distance",0.01,1e8,result.haze_distance) ||
        !scalar(haze,"anisotropy",-0.95,0.95,result.haze_anisotropy) || !vector(haze,"color",0,1,result.haze_color)) return {};
    result.capture_lighting = capture.value("mode") == "shadow-receiver" ? 2 : capture.value("mode") == "lit-color" ? 1 : 0;
  }
  QSet<QString> ids;
  const auto identity = [&ids](const QJsonObject& o, QString& id, QString& name) {
    id = o.value("id").toString(); name = o.value("name").toString();
    if (id.isEmpty() || id.size() > 80 || ids.contains(id) || name.trimmed().isEmpty() || name.size() > 80) return false;
    ids.insert(id); return true;
  };
  if (!scalar(json, "ambient", 0, 10, result.ambient)) return {};
  size_t total_materials = 0;
  for (const auto& entry : objects) {
    if (!entry.isObject()) return {};
    const auto o = entry.toObject(); GaussianSceneObject object;
    const bool mesh = geometry && o.value("kind") == "mesh";
    const bool materials = textured && mesh && o.contains("materialSetSha256");
    if (o.size() != (materials ? 15 : mesh ? 12 : 11) || !identity(o, object.id, object.name) || !o.value("visible").isBool() ||
        (!mesh && o.value("kind") != "sphere" && o.value("kind") != "box") ||
        !vector(o, "position", -1e8, 1e8, object.position) || !vector(o, "size", 0.0001, 1e6, object.size) ||
        !vector(o, "rotation", -360, 360, object.rotation) || !vector(o, "color", 0, 1, object.color) ||
        !scalar(o, "roughness", 0.04, 1, object.roughness) || !scalar(o, "metallic", 0, 1, object.metallic) ||
        !scalar(o, "emission", 0, 100, object.emission)) return {};
    if (mesh) {
      object.mesh_sha256 = o.value("meshSha256").toString();
      if (!valid_gaussian_mesh_hash(object.mesh_sha256)) return {};
    }
    if (materials) {
      object.material_set_sha256 = o.value("materialSetSha256").toString();
      if (!valid_gaussian_mesh_hash(object.material_set_sha256) || !o.value("useImportedMaterials").isBool() || !o.value("materials").isArray()) return {};
      const auto surfaces = o.value("materials").toArray(); if (surfaces.isEmpty() || surfaces.size() > 64) return {};
      total_materials += static_cast<size_t>(surfaces.size()); if (total_materials > 256) return {};
      for (const auto& surface : surfaces) { const auto parsed = gaussian_surface_material(surface.toObject().toVariantMap()); if (!parsed) return {}; object.materials.push_back(*parsed); }
      object.use_imported_materials = o.value("useImportedMaterials").toBool();
    }
    object.kind = mesh ? 2 : o.value("kind") == "box" ? 1 : 0; object.visible = o.value("visible").toBool();
    result.objects.push_back(object);
  }
  for (const auto& entry : lights) {
    if (!entry.isObject()) return {};
    const auto o = entry.toObject(); GaussianSceneLight light;
    const auto kind = o.value("kind").toString();
    light.kind = kind == "directional" ? 0 : kind == "point" ? 1 : sampled && kind == "rectangle" ? 2 : sampled && kind == "sphere" ? 3 : -1;
    const bool finite_sun = sampled && light.kind == 0 && o.contains("angularRadius");
    const bool camera_visible = o.contains("cameraVisible");
    if (camera_visible && (!visible_sources || light.kind < 2 || !o.value("cameraVisible").isBool())) return {};
    if (light.kind < 0 || o.size() != (light.kind == 2 ? 9 : light.kind == 3 || finite_sun ? 8 : 7)+(camera_visible ? 1 : 0) || !identity(o, light.id, light.name) || !o.value("visible").isBool() ||
        !vector(o, "vector", -1e8, 1e8, light.vector) || !vector(o, "color", 0, 1, light.color) ||
        !scalar(o, "intensity", 0, 10000, light.intensity)) return {};
    light.visible = o.value("visible").toBool();
    light.camera_visible = camera_visible && o.value("cameraVisible").toBool();
    if (finite_sun && !scalar(o,"angularRadius",0,89,light.angular_radius)) return {};
    if (light.kind == 3 && !scalar(o,"radius",0.0001,1e6,light.radius)) return {};
    if (light.kind == 2) {
      const auto size = o.value("size"); if (!size.isArray() || size.toArray().size() != 2 || !vector(o,"rotation",-360,360,light.rotation)) return {};
      for (int axis = 0; axis < 2; ++axis) { const auto value = size.toArray()[axis]; if (!value.isDouble() || !std::isfinite(value.toDouble())) return {}; light.size[axis] = static_cast<float>(value.toDouble()); if (!std::isfinite(light.size[axis]) || light.size[axis] < 0.0001F || light.size[axis] > 1e6F) return {}; }
    }
    if (light.kind == 0 && light.vector.lengthSquared() < 1e-12F) return {};
    result.lights.push_back(light);
  }
  return result;
}
struct GaussianCompositionLoad {
  std::shared_ptr<const GaussianSceneComposition> composition;
  QVariantMap receipt;
  QString error;
};
// Resolve immutable SHA-bound resources for a parsed composition, including a
// frozen export recipe. Never consult the mutable scene-objects.json here.
inline GaussianCompositionLoad resolve_gaussian_composition_resources(const QString& project,
    const GaussianSceneComposition& composition, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  auto resolved = composition;
  size_t triangles = 0; QMap<QString,std::shared_ptr<const GaussianSceneMesh>> assets;
  QMap<QString,std::shared_ptr<const GaussianMaterialAssets>> material_assets;
  QMap<QString,qint64> textures;
  for (auto& object : resolved.objects) if (object.kind == 2) {
    if (!assets.contains(object.mesh_sha256)) {
      const auto asset = load_gaussian_mesh_asset(project,object.mesh_sha256,canceled);
      if (!asset.mesh) return {{},{},asset.error};
      assets.insert(object.mesh_sha256,asset.mesh); triangles += asset.mesh->triangles.size();
      if (triangles > 1000000) return {{},{},"Scene geometry exceeds one million unique triangles."};
    }
    object.mesh = assets.value(object.mesh_sha256);
    if (!object.material_set_sha256.isEmpty()) {
      if (!material_assets.contains(object.material_set_sha256)) {
        const auto loaded = load_gaussian_material_assets(project,object.material_set_sha256,object.mesh_sha256,canceled);
        if (!loaded.assets) return {{},{},loaded.error};
        material_assets.insert(object.material_set_sha256,loaded.assets);
        for (auto it = loaded.assets->textures.cbegin(); it != loaded.assets->textures.cend(); ++it) textures.insert(it.key(),it.value()->image.sizeInBytes());
      }
      object.material_assets = material_assets.value(object.material_set_sha256);
      if (object.material_assets->mesh_sha256 != object.mesh_sha256 || object.materials.size() != static_cast<size_t>(object.mesh->material_names.size())) return {{},{},"Imported materials do not match this mesh."};
      for (const auto& material : object.materials) if (!material.texture_sha256.isEmpty() && !object.material_assets->textures.contains(material.texture_sha256)) return {{},{},"A material references an unavailable color image."};
    }
  }
  qint64 pixel_bytes = 0; for (const auto size : textures) pixel_bytes += size;
  if (textures.size() > 32 || pixel_bytes > 256*1024*1024) return {{},{},"Scene color images exceed 32 images or 256 MiB of decoded pixels."};
  return {std::make_shared<GaussianSceneComposition>(std::move(resolved)), {}, {}};
}
inline GaussianCompositionLoad load_gaussian_composition(const QString& project, const QString& artifact_hash, bool resolve_geometry = true, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  QFile file(QDir(project).filePath("scene-objects.json"));
  if (QFileInfo(file.fileName()).isSymLink()) return {{}, {}, "Scene objects must remain inside the project."};
  if (!file.exists()) return {std::make_shared<GaussianSceneComposition>(), {}, {}};
  if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {{}, {}, "Cannot read the scene objects."};
  const auto root = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
  const auto parsed = gaussian_scene_composition(root.value("composition").toMap());
  if (root.size() != 3 || root.value("schema") != "looksy-scene-objects-v1" || root.value("artifactSha256") != artifact_hash || !parsed)
    return {{}, {}, "Scene objects are invalid or belong to a different Gaussian artifact."};
  if (!resolve_geometry) return {std::make_shared<GaussianSceneComposition>(*parsed),root,{}};
  auto resolved = resolve_gaussian_composition_resources(project,*parsed,canceled);
  if (resolved.composition) resolved.receipt = root;
  return resolved;
}
inline GaussianCompositionLoad save_gaussian_composition(const QString& project, const QString& artifact_hash,
    const GaussianSceneComposition& composition, const QVariantMap& expected_receipt) {
  const auto path = QDir(project).filePath("scene-objects.json");
  QLockFile lock(path + ".lock");
  if (!lock.tryLock(0)) return {{}, {}, "Another window is saving scene objects. Try again."};
  const auto existing = load_gaussian_composition(project, artifact_hash, false);
  if (!existing.error.isEmpty()) return existing;
  if (existing.receipt != expected_receipt) return {{}, {}, "Scene objects changed in another window. Reopen this project before editing."};
  if (!gaussian_scene_composition(composition.description())) return {{}, {}, "Invalid scene object or lighting settings."};
  QSet<QString> assets; size_t triangles = 0; QMap<QString,qint64> textures;
  for (const auto& object : composition.objects) if (object.kind == 2) {
    if (!object.mesh || object.mesh->sha256 != object.mesh_sha256 || !QFileInfo(gaussian_mesh_asset_path(project,object.mesh_sha256)).isFile() || QFileInfo(QDir(project).filePath("geometry")).isSymLink() || QFileInfo(gaussian_mesh_asset_path(project,object.mesh_sha256)).isSymLink()) return {{},{},"Scene geometry is unavailable. Import the OBJ again."};
    if (!assets.contains(object.mesh_sha256)) { assets.insert(object.mesh_sha256); triangles += object.mesh->triangles.size(); }
    if (!object.material_set_sha256.isEmpty()) {
      if (!object.material_assets || object.material_assets->sha256 != object.material_set_sha256 || object.material_assets->mesh_sha256 != object.mesh_sha256 ||
          object.materials.size() != static_cast<size_t>(object.mesh->material_names.size())) return {{},{},"Imported material assets are unavailable."};
      for (const auto& material : object.materials) if (!material.texture_sha256.isEmpty() && !object.material_assets->textures.contains(material.texture_sha256)) return {{},{},"A material references an unavailable image."};
      for (auto it = object.material_assets->textures.cbegin(); it != object.material_assets->textures.cend(); ++it) textures.insert(it.key(),it.value()->image.sizeInBytes());
    }
  }
  if (triangles > 1000000) return {{},{},"Scene geometry exceeds one million unique triangles."};
  qint64 pixel_bytes = 0; for (const auto size : textures) pixel_bytes += size;
  if (textures.size() > 32 || pixel_bytes > 256*1024*1024) return {{},{},"Scene color images exceed their memory limit."};
  const QVariantMap receipt{{"schema", "looksy-scene-objects-v1"}, {"artifactSha256", artifact_hash}, {"composition", composition.description()}};
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson(); QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return {{}, {}, "Cannot save scene objects. The previous scene is unchanged."};
  return {std::make_shared<GaussianSceneComposition>(composition), receipt, {}};
}
}  // namespace photo_editor::desktop
