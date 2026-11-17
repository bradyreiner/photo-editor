#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <thread>
#include <iostream>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
namespace {
struct Observation { GaussianFrame final; std::vector<GaussianFrame> partials; std::vector<QImage> pixels; double first_ms = -1, total_ms = 0; };
Observation observe(const std::shared_ptr<GaussianSceneRenderer>& renderer,const std::shared_ptr<const GaussianSceneData>& scene,
    const GaussianCamera& camera,bool interactive,bool retain,bool cancel_first = false) {
  Observation result; const auto queue = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions options; options.progressive_frames = queue; options.interactive = interactive; options.canceled = std::make_shared<std::atomic_bool>(false);
  std::atomic_bool done{false}; QElapsedTimer timer; timer.start();
  std::jthread worker([&] { result.final = renderer->renderWithOptions(scene,camera,false,{},options); done.store(true); });
  const auto take = [&] {
    if (auto frame = queue->takeLatest()) {
      if (result.first_ms < 0) result.first_ms = static_cast<double>(timer.nsecsElapsed())/1e6;
      if (retain) { result.pixels.push_back(read_gaussian_photo_texture(*frame)); result.partials.push_back(std::move(*frame)); }
      else { result.partials.clear(); result.partials.push_back(std::move(*frame)); }
      if (cancel_first) options.canceled->store(true);
    }
  };
  while (!done.load()) { take(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  worker.join(); result.total_ms = static_cast<double>(timer.nsecsElapsed())/1e6; take();
  if (result.first_ms < 0) result.first_ms = result.total_ms;
  return result;
}
QByteArray read_file(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
GaussianPoint point(QVector3D position,QVector3D color,float opacity,float scale) {
  GaussianPoint p; p.center_opacity = {position.x(),position.y(),position.z(),opacity};
  p.covariance_a = {scale*scale,0,0,scale*scale}; p.covariance_b = {0,scale*scale,0,0};
  for (int c = 0; c < 3; ++c) p.sh[static_cast<size_t>(c)] = (color[c]-0.5F)/0.2820947918F;
  return p;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); const auto args = app.arguments();
  const auto option = [&](const QString& name) { const auto i = args.indexOf(name); return i < 0 ? QString{} : args.value(i+1); };
  const auto proof = option("--proof-dir");
  if (!proof.isEmpty() && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto fail = [](const char* reason) { std::cerr << "Progressive lighting: " << reason << '\n'; return 1; };
  if (args.contains("--dense-project")) {
    const auto project = option("--dense-project"); const auto source = QJsonDocument::fromJson(read_file(option("--recipe"))).toVariant().toMap();
    const auto composition = gaussian_scene_composition(source.value("sceneComposition").toMap()); const auto data = source.value("camera").toMap();
    const auto manifest = QJsonDocument::fromJson(read_file(project+"/scene.json")).toVariant().toMap();
    const auto loaded = load_gaussian_scene(project+"/trained/scene.ply",project+"/dataset/sparse",manifest.value("artifactValidation").toMap().value("artifactSha256").toString());
    if (proof.isEmpty() || !loaded.scene || !composition || data.isEmpty()) return 2;
    GaussianCamera camera; const auto xyz = [&](const char* k) { const auto v = data.value(k).toList(); return QVector3D(v.value(0).toFloat(),v.value(1).toFloat(),v.value(2).toFloat()); };
    camera.position = xyz("position"); camera.right = xyz("right"); camera.down = xyz("down"); camera.forward = xyz("forward");
    camera.fx = data.value("fx").toFloat(); camera.fy = data.value("fy").toFloat(); camera.cx = data.value("cx").toFloat(); camera.cy = data.value("cy").toFloat();
    camera.image_size = {data.value("width").toInt(),data.value("height").toInt()}; camera.composition = std::make_shared<GaussianSceneComposition>(*composition);
    auto renderer = make_gaussian_scene_renderer(); const auto base = camera; QVariantList frames;
    for (int i = 0; i < 3; ++i) {
      camera = i == 0 ? base : navigate_gaussian_camera(base,loaded.scene->focus_distance,0.01F*static_cast<float>(i),0,0,0,0).resized({720,480});
      const auto observed = observe(renderer,loaded.scene,camera,i != 0,false);
      const auto& final = observed.final; const auto pixels = read_gaussian_photo_texture(final); const auto filename = QStringLiteral("frame-%1.png").arg(i);
      if (!final.error.isEmpty() || final.canceled || pixels.isNull() || !pixels.save(proof+"/"+filename)) return fail("dense final frame");
      if (!observed.partials.empty() && !read_gaussian_photo_texture(observed.partials.back()).save(proof+QStringLiteral("/partial-%1.png").arg(i))) return 1;
      frames.append(QVariantMap{{"file",filename},{"firstAvailableMs",observed.first_ms},{"completedMs",observed.total_ms},
          {"firstWasFinal",observed.partials.empty()},{"metrics",final.metrics}});
      const QVariantMap receipt{{"passed",i == 2},{"camera",base.description()},{"composition",composition->description()},{"artifactSha256",loaded.scene->artifact_sha256},
          {"frames",frames},{"scope","same full-resolution photo and two 720x480 interactive poses; first immutable GPU queue availability, not input-to-photon latency"}};
      QFile file(proof+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(receipt).toJson(); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2;
      std::cerr << i << " first=" << observed.first_ms << " completed=" << observed.total_ms << '\n';
    }
    return 0;
  }
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point({-0.8F,0,1.2F},{0.9F,0.2F,0.1F},0.8F,0.5F),point({0.8F,0.4F,1.8F},{0.1F,0.4F,0.9F},0.9F,0.3F)};
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  GaussianSceneComposition composition; composition.ambient = 0; composition.environment_diffuse = true; composition.spatial_environment = true; composition.surface_bounces = 4; composition.light_samples = 16;
  GaussianSceneObject receiver; receiver.id = "receiver"; receiver.name = "Receiver"; receiver.position = {0,0,3}; receiver.size = {1.2F,1.2F,1.2F}; receiver.color = {0.7F,0.7F,0.7F};
  composition.objects = {receiver}; GaussianSceneLight light; light.id = "key"; light.name = "Key"; light.kind = 3; light.vector = {-1,-1,1}; light.intensity = 1; light.radius = 0.5F; composition.lights = {light};
  QVariantList cases;
  for (int mode = 0; mode < 5; ++mode) {
    composition.environment_diffuse = mode != 3; composition.spatial_environment = mode != 2; composition.scan_shadows = mode == 2;
    composition.reflections = mode == 1; composition.spatial_reflections = mode == 1; composition.surface_bounces = mode == 2 || mode == 3 ? 1 : 4;
    camera.photo = {}; if (mode == 1) { camera.photo.film = {0.1F,2,0.15F,3,0.7F,123}; camera.photo.tone = {1.1F,0.6F}; }
    if (mode == 4) camera.photo.lens = {3.0F,0.03F,0,0,0,16};
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    const auto expected = make_gaussian_scene_renderer()->render(scene,camera,true); auto renderer = make_gaussian_scene_renderer();
    const auto observed = observe(renderer,scene,camera,false,true);
    if (!expected.error.isEmpty() || expected.image.isNull() || !observed.final.error.isEmpty() || observed.partials.empty() ||
        observed.final.metrics.value("lightingProgressiveFramesProduced").toInt() < 1 || read_gaussian_photo_texture(observed.final) != expected.image) return fail("final exact parity / missing lighting publication");
    int previous_lighting = 0;
    for (size_t i = 0; i < observed.partials.size(); ++i) {
      const auto& frame = observed.partials[i]; const int samples = frame.metrics.value("softLightSamples").toInt();
      if (!frame.image.isNull() || frame.metrics.value("previewReadback").toBool() || observed.pixels[i].isNull() || read_gaussian_photo_texture(frame) != observed.pixels[i]) return fail("snapshot mutated or preview read back");
      if (frame.metrics.value("progressiveLighting").toBool() && (samples <= previous_lighting || samples >= composition.light_samples)) return fail("invalid lighting prefix");
      previous_lighting = samples;
    }
    auto changed = camera; changed.position.setX(0.2F); renderer->render(scene,changed,false);
    for (size_t i = 0; i < observed.partials.size(); ++i) if (read_gaussian_photo_texture(observed.partials[i]) != observed.pixels[i]) return fail("later render overwrote retained snapshot");
    const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions export_options; export_options.progressive_frames = queue;
    if (make_gaussian_scene_renderer()->renderWithOptions(scene,camera,true,{},export_options).image != expected.image || queue->takeLatest()) return fail("readback export published a preview or changed pixels");
    cases.append(QVariantMap{{"mode",mode},{"exactFinalPixels",true},{"immutableSnapshots",static_cast<int>(observed.partials.size())},{"metrics",observed.final.metrics}});
  }
  camera.photo = {}; camera.image_size = {513,513}; camera.fx = camera.fy = 480; camera.cx = camera.cy = 256.5F;
  composition.light_samples = 1024; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto interrupted = observe(make_gaussian_scene_renderer(),scene,camera,false,true,true);
  if (interrupted.partials.empty() || !interrupted.final.canceled || !interrupted.final.image.isNull() || interrupted.final.native_texture) return fail("post-publication cancellation exposed a final frame");
  const QVariantMap receipt{{"passed",true},{"cases",cases},{"observedPostPublicationCancellation",true},{"scope","exact final/export parity, lighting/lens prefixes, immutable native textures, later renders and cancellation; latency is a separate dense/native gate"}};
  if (!proof.isEmpty()) { QFile file(proof+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(receipt).toJson(); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2; }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
}
