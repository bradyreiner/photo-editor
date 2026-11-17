#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QFile>
#include <thread>
#include <iostream>
#include <cmath>

using namespace photo_editor::desktop;
namespace {
const QVector3D normal{float(std::sqrt(0.75)),0,-0.5F}, center{0,0,3};
GaussianPoint point(QVector3D position) {
  GaussianPoint p; p.center_opacity = {position.x(),position.y(),position.z(),1};
  const auto cov = [](int a,int b) { return (a == b ? 0.04F : 0)+(0.000001F-0.04F)*normal[a]*normal[b]; };
  p.covariance_a = {cov(0,0),cov(0,1),cov(0,2),cov(1,1)}; p.covariance_b = {cov(1,2),cov(2,2),0,0};
  for (size_t c = 0; c < 3; ++c) p.sh[c] = (0.6F-0.5F)/0.2820947918F;
  return p;
}
bool write(QString path,QByteArray data) { QFile f(path); return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.write(data) == data.size(); }
GaussianSceneObject mesh_emitter(const QString& root) {
  const QVector3D tangent{0.5F,0,float(std::sqrt(0.75))};
  QByteArray obj("mtllib panel.mtl\n");
  for (const auto& uv : {QVector2D(-1,-1),QVector2D(1,-1),QVector2D(1,1),QVector2D(-1,1)}) {
    const auto v = center+normal*2+tangent*(uv.x()*0.4F)+QVector3D(0,uv.y()*0.4F,0);
    obj += "v "+QByteArray::number(v.x(),'g',9)+' '+QByteArray::number(v.y(),'g',9)+' '+QByteArray::number(v.z(),'g',9)+'\n';
  }
  obj += "usemtl Panel\nf 1 2 3 4\n";
  const auto path = QDir(root).filePath("panel.obj");
  if (!write(path,obj) || !write(QDir(root).filePath("panel.mtl"),"newmtl Panel\nKd 1 1 1\nKe 1 0 0\n")) return {};
  const auto mesh = load_gaussian_obj(path); if (!mesh.mesh) return {};
  const auto materials = load_gaussian_obj_materials(path,*mesh.mesh); if (!materials.assets) return {};
  GaussianSceneObject o; o.id = "mesh"; o.name = "Mesh emitter"; o.kind = 2; o.position = {}; o.size = {1,1,1};
  o.mesh = mesh.mesh; o.mesh_sha256 = mesh.mesh->sha256; o.materials = materials.assets->materials;
  o.material_set_sha256 = materials.assets->sha256; o.material_assets = materials.assets; return o;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); QTemporaryDir temporary;
  const auto args = app.arguments(); const auto proof_index = args.indexOf("--proof-dir"); const auto proof = proof_index < 0 ? QString() : args.value(proof_index+1);
  if (!proof.isEmpty() && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point(center),point({0.6F,0.4F,3.1F})};
  auto isotropic = point({-0.4F,0.3F,3}); isotropic.covariance_a = {0.01F,0,0,0.01F}; isotropic.covariance_b = {0,0.01F,0,0};
  scene->points.push_back(isotropic);
  GaussianSceneComposition composition; composition.capture_lighting = 1; composition.capture_emission = true;
  composition.capture_emission_importance = true; composition.light_samples = 64; composition.scan_shadows = true;
  composition.ambient = 0.15F;
  GaussianSceneObject sphere; sphere.id = "emitter"; sphere.name = "Emitter"; sphere.position = center+normal*2;
  sphere.size = {0.4F,0.4F,0.4F}; sphere.color = {1,0.2F,0.05F}; sphere.emission = 5; composition.objects = {sphere};
  GaussianSceneLight light; light.id = "point"; light.name = "Point"; light.kind = 1; light.vector = {1,0,2}; light.intensity = 0.5F;
  composition.lights = {light};
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  auto renderer = make_gaussian_scene_renderer(); GaussianRenderOptions options; std::optional<double> seconds;
  QVariantList cases;
  const auto rotate = [&] {
    const auto rotation = QQuaternion::fromAxisAndAngle(QVector3D(0,1,0),0.5F);
    camera.right = rotation.rotatedVector(camera.right); camera.down = rotation.rotatedVector(camera.down); camera.forward = rotation.rotatedVector(camera.forward);
  };
  const auto check = [&](const char* name,bool expected_hit,bool expected_evaluation = true) {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    const auto frame = renderer->renderWithOptions(scene,camera,true,seconds,options);
    const auto fresh = make_gaussian_scene_renderer()->renderWithOptions(scene,camera,true,seconds,options);
    const bool valid = frame.error.isEmpty() && fresh.error.isEmpty() && !frame.image.isNull() && frame.image == fresh.image &&
        frame.metrics.value("captureLightingReused").toBool() == expected_hit &&
        frame.metrics.value("captureLightingEvaluated").toBool() == (expected_evaluation && !expected_hit);
    cases.append(QVariantMap{{"name",name},{"passed",valid},{"exactFreshPixels",frame.image == fresh.image},{"metrics",frame.metrics}});
    if (!proof.isEmpty() && !frame.image.isNull()) frame.image.save(proof+"/"+QString::fromLatin1(name)+".png");
    if (!valid) std::cerr << name << ": " << frame.error.toStdString() << ' ' << fresh.error.toStdString() << " hit=" << frame.metrics.value("captureLightingReused").toBool() << " evaluated=" << frame.metrics.value("captureLightingEvaluated").toBool() << '\n';
    return valid;
  };
  if (!check("initial",false)) return 1;
  rotate(); if (!check("view-rotation",true)) return 1;
  camera = camera.resized({81,65}); if (!check("raster-grid",true)) return 1;
  camera.photo.exposure_ev = 0.5F; rotate(); if (!check("exposure-and-view",true)) return 1;
  camera.position.setX(0.3F); if (!check("observer-position",false)) return 1;
  const auto original_side = camera;
  camera.position = {0,0,6}; camera.right = {-1,0,0}; camera.down = {0,1,0}; camera.forward = {0,0,-1};
  if (!check("opposite-normal-hemisphere",false)) return 1;
  camera = original_side; if (!check("restored-normal-hemisphere",false)) return 1;
  composition.light_samples = 16; if (!check("sample-quality",false)) return 1;
  options.interactive = true; if (!check("interactive-quality",false)) return 1;
  rotate(); if (!check("interactive-view",true)) return 1;
  options.interactive = false; composition.light_samples = 64; if (!check("restored-final-quality",false)) return 1;
  composition.capture_power = 0.7F; if (!check("capture-power",false)) return 1;
  composition.objects[0].position.setY(0.1F); if (!check("object-transform",false)) return 1;
  composition.objects[0].color = {0.1F,0.4F,1}; if (!check("emitter-color",false)) return 1;
  composition.objects[0].emission = 2; if (!check("emitter-power",false)) return 1;
  composition.alpha_model = GaussianAlphaModel::Inria99; if (!check("alpha-model",false)) return 1;
  composition.shadow_strength = 0.5F; if (!check("shadow-strength",false)) return 1;
  composition.scan_shadows = false; if (!check("scan-visibility",false)) return 1;
  composition.haze_density = 0.1F; if (!check("emitter-haze",false)) return 1;
  composition.lights[0].intensity = 1; if (!check("direct-light",false)) return 1;
  composition.capture_lighting = 0; if (!check("captured-mode",false,false)) return 1;
  composition.capture_lighting = 1; if (!check("restored-lit-mode",true)) return 1;
  composition.objects[0] = mesh_emitter(temporary.path()); if (!composition.objects[0].mesh || !check("imported-emitter",false)) return 1;
  for (auto& material : composition.objects[0].materials) if (material.emission.lengthSquared() > 0) material.emission = {0,1,0};
  if (!check("material-emission",false)) return 1;
  rotate(); if (!check("imported-view",true)) return 1;
  auto replacement = std::make_shared<GaussianSceneData>(*scene); replacement->points[0].covariance_a[0] *= 1.2F; scene = replacement;
  if (!check("scene-identity",false)) return 1;
  const auto sampled_recipe = composition;
  composition.capture_emission = false;
  if (!check("unbatched-hard-lighting",false)) return 1;
  rotate(); if (!check("unbatched-view",true)) return 1;
  composition = sampled_recipe;
  if (!check("restored-sampled-lighting",false)) return 1;
  // A canceled lighting update must not leave the old key attached to a
  // partially overwritten buffer, even if the previous scene recipe returns.
  const auto accepted = composition; composition.light_samples = 1024; composition.capture_power = 1.3F;
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto canceled = std::make_shared<std::atomic_bool>(false); const auto batches = std::make_shared<std::atomic_int>(0);
  GaussianRenderOptions cancel_options; cancel_options.canceled = canceled; cancel_options.lighting_batches_completed = batches;
  std::jthread stopper([&](std::stop_token stop) { while (!stop.stop_requested() && batches->load() == 0) std::this_thread::sleep_for(std::chrono::microseconds(100)); if (!stop.stop_requested()) canceled->store(true); });
  const auto interrupted = renderer->renderWithOptions(scene,camera,false,seconds,cancel_options);
  stopper.request_stop(); stopper.join();
  if (!canceled->load() || !interrupted.canceled || interrupted.native_texture || !interrupted.image.isNull()) { std::cerr << "Partial capture cancellation failed\n"; return 1; }
  composition = accepted;
  if (!check("after-canceled-update",false)) return 1;
  composition.capture_lighting = 2;
  if (!check("shadow-receiver",false)) return 1;
  rotate(); if (!check("shadow-receiver-view",true)) return 1;
  composition.shadow_strength = 0.3F;
  if (!check("shadow-receiver-strength",false)) return 1;
  composition = accepted;
  // Conditional temporal geometry and opacity must invalidate at each time.
  replacement = std::make_shared<GaussianSceneData>(*scene); GaussianTemporalPoint motion;
  motion.velocity_time = {0.05F,0,0,0}; motion.normalization[0] = 1;
  motion.covariance_factor[0] = motion.covariance_factor[5] = 0.2F; motion.covariance_factor[10] = 0.001F;
  replacement->temporal.assign(replacement->points.size(),motion); replacement->local_time_range = {0,2}; scene = replacement; seconds = 0;
  if (!check("temporal-initial",false)) return 1;
  rotate(); if (!check("temporal-view",true)) return 1;
  seconds = 1; if (!check("temporal-time",false)) return 1;
  seconds = 0; if (!check("temporal-revisit",false)) return 1;
  // A mixed spatial/probe recipe changes the capture accumulation grouping
  // between probe faces and the main view. It cannot reuse across those groups.
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point(center)}; seconds.reset(); camera.photo = {};
  composition = {}; composition.capture_lighting = 1; composition.capture_emission = true; composition.capture_emission_importance = true;
  composition.objects = {sphere}; composition.reflections = true; composition.spatial_reflections = true; composition.environment_diffuse = true;
  camera.position = sphere.position; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto grouped = make_gaussian_scene_renderer()->render(scene,camera,true);
  if (!grouped.error.isEmpty() || !grouped.metrics.value("captureLightingEvaluated").toBool() || grouped.metrics.value("captureLightingReused").toBool() ||
      grouped.metrics.value("scanProbeCaptureLightingEvaluations").toInt() != 1 || grouped.metrics.value("scanProbeCaptureLightingCacheHits").toInt() != 5) {
    std::cerr << "Probe/main accumulation grouping key failed\n"; return 1;
  }
  if (!proof.isEmpty()) {
    const QVariantMap receipt{{"passed",true},{"cases",cases},{"partialCancellationInvalidates",true},{"probeMainGroupingProtected",true},
        {"scope","Cached versus independent fresh-renderer exact pixels across view/grid, settings, geometry/materials, scene/time/quality changes and canceled updates; mixed probe/main grouping key; not latency qualification"}};
    if (!write(proof+"/receipt.json",QJsonDocument::fromVariant(receipt).toJson())) return 2;
  }
  std::cout << "Capture lighting cache: " << cases.size() << " exact fresh-renderer comparisons; cancellation and probe/main grouping pass\n";
  return 0;
}
