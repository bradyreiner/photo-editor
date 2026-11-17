#include "gaussian_scene_renderer.h"
#include "gaussian_scan_visibility.h"
#include "gaussian_scene_cleanup.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <thread>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
GaussianPoint particle(QVector3D center,float opacity,float scale = 0.08F) {
  GaussianPoint p; p.center_opacity = {center.x(),center.y(),center.z(),opacity};
  p.covariance_a = {scale*scale,0,0,scale*scale}; p.covariance_b = {0,scale*scale,0,0};
  for (size_t c = 0; c < 3; ++c) p.sh[c] = -0.5F/0.2820947918F;
  return p;
}
}
#include "gaussian_scan_visibility_perf.h"
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  const auto fail = [](const char* reason) { std::cerr << "Scan shadows: " << reason << '\n'; return 1; };
  const auto args = app.arguments(); const auto proof_index = args.indexOf("--proof-dir");
  if (args.value(1) == "--visibility-perf") return gaussian_scan_visibility_perf(args);
  const auto proof = proof_index < 0 ? QString{} : args.value(proof_index+1);
  if (proof_index >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  GaussianSceneComposition composition; composition.scan_shadows = true; composition.ambient = 0;
  GaussianSceneObject floor; floor.id = "floor"; floor.name = "Receiver"; floor.kind = 1;
  floor.position = {}; floor.size = {3,3,0.01F}; floor.color = {0.6F,0.6F,0.6F}; floor.roughness = 1;
  composition.objects.push_back(floor);
  GaussianSceneLight light; light.id = "key"; light.name = "Key"; light.vector = {0,0,-1}; light.intensity = 3;
  composition.lights.push_back(light);
  const auto description = composition.description(); const auto parsed = gaussian_scene_composition(description);
  if (!parsed || !parsed->scan_shadows || parsed->description() != description || description.value("schema") != "looksy-scene-composition-v10") return fail("v10 roundtrip");
  for (const auto& invalid : {QVariant(1),QVariant("true"),QVariant()}) {
    auto broken = description; broken["scanShadows"] = invalid;
    if (gaussian_scene_composition(broken)) return fail("invalid shadow flag accepted");
  }
  auto broken = description; broken.remove("scanShadows"); if (gaussian_scene_composition(broken)) return fail("missing shadow flag accepted");
  broken = description; broken["schema"] = "looksy-scene-composition-v9"; if (gaussian_scene_composition(broken)) return fail("old schema accepted new field");
  QTemporaryDir project;
  if (!save_gaussian_composition(project.path(),"artifact",composition,{}).composition ||
      !load_gaussian_composition(project.path(),"artifact").composition->scan_shadows) return fail("persistence");
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-1},0.6F)};
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.position = {2,0,-3}; camera.forward = (QVector3D(0,0,-0.01F)-camera.position).normalized();
  camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized(); camera.down = {0,1,0};
  auto renderer = make_gaussian_scene_renderer();
  const auto render = [&](bool enabled,std::optional<double> time = {}) {
    composition.scan_shadows = enabled; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    return renderer->render(scene,camera,true,time);
  };
  double max_ratio_error = 0; QVariantList cases;
  const auto check = [&](double expected,const char* name,std::optional<double> time = {},double tolerance = 0.018) {
    const auto clear = render(false,time), blocked = render(true,time);
    if (!clear.error.isEmpty() || !blocked.error.isEmpty()) { std::cerr << clear.error.toStdString() << blocked.error.toStdString() << '\n'; return false; }
    const double reference = decode(clear.image.pixelColor(32,32).redF());
    const double ratio = decode(blocked.image.pixelColor(32,32).redF())/reference;
    max_ratio_error = std::max(max_ratio_error,std::abs(ratio-expected));
    cases.append(QVariantMap{{"case",name},{"ratio",ratio},{"expected",expected},{"metrics",blocked.metrics}});
    if (!proof.isEmpty() && (!clear.image.save(proof+"/"+name+"-off.png") || !blocked.image.save(proof+"/"+name+"-on.png"))) return false;
    if (std::abs(ratio-expected) > tolerance) { std::cerr << name << " ratio=" << ratio << " expected=" << expected << '\n'; return false; }
    return true;
  };
  if (!check(0.4,"single-opacity")) return fail("single opacity oracle");
  const auto reused = render(true);
  if (!reused.metrics.value("scanShadowIndexReused").toBool() || reused.metrics.value("scanShadowBuildMs").toDouble() != 0) return fail("completed index reuse");
  // Multiple overlapping translucent splats, spanning several BVH leaves.
  scene = std::make_shared<GaussianSceneData>();
  for (int i = 0; i < 48; ++i) scene->points.push_back(particle({0,0,-0.5F-static_cast<float>(i)*0.02F},0.025F));
  if (!check(std::pow(0.975,48),"overlap")) return fail("multi-leaf product oracle");
  // Correlated covariance: eliminate z analytically. The peak response on a
  // z ray depends on the XY marginal, not on the orthogonal centre distance.
  scene = std::make_shared<GaussianSceneData>(); auto tilted = particle({0.08F,0,-1},0.7F);
  tilted.covariance_a = {0.01F,0,0.019F,0.0025F}; tilted.covariance_b = {0,0.04F,0,0}; scene->points = {tilted};
  if (!check(1-0.7*std::exp(-0.5*0.08*0.08/0.01),"anisotropic")) return fail("anisotropic peak oracle");
  scene = std::make_shared<GaussianSceneData>(); auto thin = particle({0,0,-0.5F},0.6F);
  thin.covariance_a = {0.005F,0,0.005F,0.0064F}; thin.covariance_b = {0,0.005F,0,0};
  scene->points = {thin,particle({2,0,-0.5F},0.6F)};
  scene->spatial_factors = {{0.07071068F,0,0.07071068F,0,0,0.08F,0,0,-0.0000007071068F,0,0.0000007071068F,0},
      {0.08F,0,0,0,0,0.08F,0,0,0,0,0.08F,0}};
  if (!check(0.4,"thin-factor")) return fail("preserved thin static factor");
  const auto cleaned = apply_gaussian_cleanup(scene,QByteArray("\0\1",2),{}, {},false);
  if (!cleaned.scene || cleaned.scene->spatial_factors.size() != 1 || cleaned.scene->spatial_factors[0] != scene->spatial_factors[1]) return fail("cleanup factor alignment");
  // A point source before the splat must not be shadowed by that splat.
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-2},0.9F)};
  composition.lights[0].kind = 1; composition.lights[0].vector = {0,0,-1};
  if (!check(1,"behind-source")) return fail("finite segment cutoff");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-0.5F},0.9F)};
  if (!check(0.1,"before-source")) return fail("finite segment blocker");
  // Time-dependent means and opacity are rebuilt for each selected instant.
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-0.5F},0.8F)};
  scene->time_origin_seconds = 100; scene->local_time_range = {0,2};
  GaussianTemporalPoint motion; motion.velocity_time = {0.5F,0,0,0}; motion.normalization[0] = 1;
  motion.covariance_factor = {0.08F,0,0,0,0,0.08F,0,0,0,0,0.08F,0}; scene->temporal = {motion};
  if (!check(0.2,"temporal-start",100) || !check(1,"temporal-moved",101) || !check(0.2,"temporal-revisit",100)) return fail("temporal index key");
  scene = std::make_shared<GaussianSceneData>(*scene); scene->temporal[0].velocity_time[0] = 0;
  if (!check(1-0.8*std::exp(-0.5),"temporal-fade",101)) return fail("temporal opacity");
  // Cancel a real time-change rebuild, then revisit the previous time on the
  // same renderer. Geometry, particles and time must remain one complete cache.
  auto rebuilding = std::make_shared<GaussianSceneData>(*scene);
  for (int i = 0; i < 200000; ++i) {
    rebuilding->points.push_back(particle({100+static_cast<float>(i)*0.001F,100,100},0.02F));
    rebuilding->temporal.push_back(rebuilding->temporal.front());
  }
  composition.scan_shadows = true; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto before_rebuild = renderer->render(rebuilding,camera,true,100);
  const auto rebuild_cancel = std::make_shared<std::atomic_bool>(false);
  std::atomic_bool canceler_started{false};
  std::jthread stop_rebuild([&] { canceler_started.store(true); std::this_thread::sleep_for(std::chrono::milliseconds(5)); rebuild_cancel->store(true); });
  while (!canceler_started.load()) std::this_thread::yield();
  GaussianRenderOptions rebuilding_options; rebuilding_options.canceled = rebuild_cancel;
  const auto canceled_rebuild = renderer->renderWithOptions(rebuilding,camera,true,101,rebuilding_options); stop_rebuild.join();
  const auto revisit = renderer->render(rebuilding,camera,true,100);
  if (!before_rebuild.error.isEmpty() || !canceled_rebuild.canceled || !canceled_rebuild.image.isNull() || canceled_rebuild.native_texture ||
      !revisit.error.isEmpty() || revisit.image != before_rebuild.image) return fail("canceled time rebuild corrupted the previous cache");
  // Zero admitted opacity is a valid empty index. Replacing a cleaned scene
  // must discard the previous accelerator, not retain its blockers.
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-0.5F},0)};
  if (!check(1,"cleaned-empty")) return fail("empty index / scene replacement");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-0.5F},1)};
  if (!check(0.001,"brush-alpha-cap",{},0.001)) return fail("Brush secondary alpha cap");
  composition.alpha_model = GaussianAlphaModel::Inria99;
  if (!check(0.01,"inria-alpha-cap",{},0.001)) return fail("original secondary alpha cap");
  composition.alpha_model = GaussianAlphaModel::Brush999;
  // An area source produces partial visibility across the Gaussian blocker.
  composition.lights[0].kind = 2; composition.lights[0].vector = {0,0,-1};
  composition.lights[0].size = {0.7F,0.7F}; composition.light_samples = 64;
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle({0,0,-0.5F},0.9F)};
  const auto area_off = render(false), area_on = render(true);
  const double area_ratio = decode(area_on.image.pixelColor(32,32).redF())/decode(area_off.image.pixelColor(32,32).redF());
  if (!area_off.error.isEmpty() || !area_on.error.isEmpty() || area_ratio < 0.3 || area_ratio > 0.9) return fail("finite-source partial visibility");
  cases.append(QVariantMap{{"case","area-penumbra"},{"ratio",area_ratio},{"metrics",area_on.metrics}});
  GaussianRenderOptions tiled; tiled.export_tile_edge = 32;
  const auto tile_frame = renderer->renderWithOptions(scene,camera,true,{},tiled);
  if (!tile_frame.error.isEmpty() || tile_frame.image != area_on.image || tile_frame.metrics.value("lightingBatches").toInt() <= area_on.metrics.value("lightingBatches").toInt()) return fail("exact shadow tiling");
  const auto cancel_batches = std::make_shared<std::atomic_int>(0), observed = std::make_shared<std::atomic_int>(0);
  const auto rendering_cancel = std::make_shared<std::atomic_bool>(false);
  camera.image_size = {513,513}; camera.cx = camera.cy = 256.5F; camera.fx = camera.fy = 480;
  std::jthread canceler([&] { QElapsedTimer elapsed; elapsed.start(); while (cancel_batches->load() == 0 && elapsed.elapsed() < 5000) std::this_thread::yield(); observed->store(cancel_batches->load()); rendering_cancel->store(true); });
  GaussianRenderOptions options; options.canceled = rendering_cancel; options.lighting_batches_completed = cancel_batches;
  const auto interrupted = renderer->renderWithOptions(scene,camera,true,{},options); canceler.join();
  if (!interrupted.canceled || observed->load() < 1 || !interrupted.image.isNull() || interrupted.native_texture) return fail("observed-batch cancellation published partial output");
  camera.image_size = {65,65}; camera.cx = camera.cy = 32.5F; camera.fx = camera.fy = 60;
  camera.photo.exposure_ev = 4; composition.environment_diffuse = true; composition.lights.clear();
  scene = std::make_shared<GaussianSceneData>();
  for (const auto center : {QVector3D(10,0,0),QVector3D(-10,0,0),QVector3D(0,10,0),QVector3D(0,-10,0),QVector3D(0,0,10),QVector3D(0,0,-10)}) {
    auto p = particle(center,0.2F,10000); p.sh.fill(0); scene->points.push_back(p);
  }
  // Broad kernels have almost constant 0.2 response over the hemisphere.
  // Ratio cancels the independently rendered probe, Lambert and Fresnel terms.
  if (!check(std::pow(0.8,6),"environment-visibility")) return fail("environment transmittance");
  const auto cancel = std::make_shared<std::atomic_bool>(true);
  if (!build_gaussian_scan_index(*scene,0,cancel).canceled) return fail("index cancellation");
  const auto dense_index = args.indexOf("--dense-project");
  if (dense_index >= 0) {
    const auto path = args.value(dense_index+1); QFile manifest(path+"/scene.json");
    if (!manifest.open(QIODevice::ReadOnly)) return fail("dense manifest");
    const auto root = QJsonDocument::fromJson(manifest.readAll()).toVariant().toMap();
    const auto loaded = load_gaussian_scene(path+"/trained/scene.ply",path+"/dataset/sparse",root.value("artifactValidation").toMap().value("artifactSha256").toString());
    if (!loaded.scene || loaded.scene->cameras.empty()) return fail("dense scene");
    camera = loaded.scene->cameras.front().resized({800,533});
    composition = {}; composition.ambient = 0.12F;
    GaussianSceneObject ball; ball.id = "ball"; ball.name = "Inserted receiver";
    const auto focus = loaded.scene->focus_distance;
    ball.position = camera.position+camera.forward*(focus*0.9F)+camera.right*(focus*0.18F);
    ball.size = QVector3D(1,1,1)*(focus*0.12F); ball.color = {0.8F,0.65F,0.4F};
    composition.objects = {ball};
    light.vector = (-camera.right*0.8F-camera.down*0.2F-camera.forward*0.3F).normalized(); composition.lights = {light};
    const auto dense_render = [&](bool enabled,bool readback) {
      composition.scan_shadows = enabled; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
      return renderer->render(loaded.scene,camera,readback);
    };
    const auto clear = dense_render(false,true), shaded = dense_render(true,true);
    if (!clear.error.isEmpty() || !shaded.error.isEmpty()) { std::cerr << shaded.error.toStdString() << '\n'; return fail("dense render"); }
    if (!proof.isEmpty() && (!clear.image.save(proof+"/dense-off.png") || !shaded.image.save(proof+"/dense-on.png"))) return fail("dense images");
    quint64 changed = 0; for (int y = 0; y < clear.image.height(); ++y) for (int x = 0; x < clear.image.width(); ++x) if (clear.image.pixel(x,y) != shaded.image.pixel(x,y)) ++changed;
    if (!changed) return fail("dense shading had no visible effect");
    QVariantList motion_metrics;
    const auto base = camera;
    for (int i = 0; i < 12; ++i) {
      camera = navigate_gaussian_camera(base,focus,static_cast<float>(i)*0.05F,0,0,0,0);
      const auto frame = dense_render(true,false);
      if (!frame.error.isEmpty() || !frame.image.isNull() || !frame.native_texture || !frame.metrics.value("scanShadowIndexReused").toBool()) return fail("dense native motion / index reuse");
      motion_metrics.append(frame.metrics);
    }
    cases.append(QVariantMap{{"case","dense"},{"artifactSha256",loaded.scene->artifact_sha256},{"pointCount",static_cast<qulonglong>(loaded.scene->points.size())},
        {"changedPixels",changed},{"off",clear.metrics},{"cold",shaded.metrics},{"nativeMotion",motion_metrics}});
  }
  const QVariantMap receipt{{"passed",true},{"maxRatioError",max_ratio_error},{"cases",cases},
      {"scope","independent finite-segment opacity, anisotropy, overlap, time, cache and persistence oracles; not photographic parity"}};
  if (!proof.isEmpty()) { QFile file(proof+"/receipt.json"); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return 2; }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
}
