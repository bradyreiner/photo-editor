#include "gaussian_scene_renderer.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <thread>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool require(bool pass,const char* reason) { if (!pass) std::cerr << reason << '\n'; return pass; }
double encoded(double v) { return v <= 0.0031308 ? 12.92*v : 1.055*std::pow(v,1/2.4)-0.055; }
double linear(int v) { const double c = double(v)/255; return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
bool near(const QImage& a,const QImage& b,int tolerance = 1) {
  if (a.isNull() || a.size() != b.size()) return false;
  for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
    const auto p = a.pixelColor(x,y),q = b.pixelColor(x,y);
    if (std::max({std::abs(p.red()-q.red()),std::abs(p.green()-q.green()),std::abs(p.blue()-q.blue())}) > tolerance) return false;
  }
  return true;
}
GaussianPoint point() {
  GaussianPoint p; p.center_opacity = {0,0,4,0.5F}; p.covariance_a = {0.04F,0,0,0.04F}; p.covariance_b = {0,0.0001F,0,0};
  p.sh[0] = p.sh[1] = p.sh[2] = 0.5F/0.2820947918F; return p;
}
}
#include "gaussian_emitter_visibility_test.h"
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  const auto args = app.arguments(); const int proof_option = static_cast<int>(args.indexOf("--proof-dir"));
  const auto proof = proof_option >= 0 ? args.value(proof_option+1) : QString();
  if (proof_option >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  if (!gaussian_primary_emitters_regression(proof)) return 1;
  auto renderer = make_gaussian_scene_renderer();
  GaussianCamera camera; camera.position = {0,0,2}; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point()};
  GaussianSceneComposition composition; composition.capture_lighting = 1; composition.ambient = 0;
  GaussianSceneLight light; light.id = "light"; light.name = "Emitter"; light.kind = 3; light.vector = {}; light.radius = 1; light.intensity = 4;
  composition.lights = {light};
  const auto render = [&] { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true); };
  const auto sphere = render();
  composition.lights[0].camera_visible = true;
  if (!require(near(render().image,sphere.image,0),"Showing a source behind the camera changed illumination")) return 1;
  composition.lights[0].camera_visible = false;
  // E/pi = L*(R/d)^2 on a normal plane. Half opacity is composited using
  // the existing capture convention, independent of the light sampler.
  if (!require(sphere.error.isEmpty() && std::abs(sphere.image.pixelColor(64,64).red()-std::lround(127.5*encoded(0.25))) <= 1 &&
      sphere.metrics.value("softLightSamples").toInt() == 64,"Spherical light differs from closed-form irradiance")) { std::cerr << sphere.error.toStdString(); return 1; }
  composition.lights[0].radius = 0.5F; const auto smaller = render();
  if (!require(std::abs(smaller.image.pixelColor(64,64).red()-std::lround(127.5*encoded(0.0625))) <= 1,"Sphere radius did not change its emitted solid angle")) return 1;
  composition.lights[0] = light; composition.lights[0].vector = {0,0,-4};
  if (!require(std::abs(render().image.pixelColor(64,64).red()-std::lround(127.5*encoded(0.0625))) <= 1,"Spherical light distance falloff is incorrect")) return 1;
  composition.lights[0] = light; composition.light_samples = 1024;
  const auto high = render(); if (!require(near(high.image,sphere.image,1),"High-quality sphere integration changed analytic energy")) return 1;
  GaussianRenderOptions draft; draft.interactive = true; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto interactive = renderer->renderWithOptions(scene,camera,true,{},draft);
  if (!require(interactive.error.isEmpty() && interactive.metrics.value("softLightSamples").toInt() == 8 && interactive.metrics.value("softLightTargetSamples").toInt() == 1024 && interactive.metrics.value("softLightDraft").toBool(),"Soft light draft/final quality was not explicit")) return 1;
  composition.light_samples = 64; composition.lights[0].kind = 0; composition.lights[0].vector = {0,0,-1}; composition.lights[0].intensity = static_cast<float>(M_PI); composition.lights[0].angular_radius = 60;
  const auto sun = render();
  if (!require(sun.error.isEmpty() && std::abs(sun.image.pixelColor(64,64).red()-std::lround(127.5*encoded(0.75))) <= 1,"Finite sun cone differs from its analytic angular average")) return 1;
  composition.lights[0].angular_radius = 0; const auto delta = render();
  if (!require(!delta.metrics.contains("softLightModel") && std::abs(delta.image.pixelColor(64,64).red()-128) <= 1,"Zero angular radius did not retain the original directional light")) return 1;
  composition.light_samples = 1024; composition.lights[0] = light; composition.lights[0].kind = 2; composition.lights[0].vector = {2,0,1}; composition.lights[0].size = {2,1}; composition.lights[0].intensity = 40;
  const auto rectangle = render();
  GaussianSceneObject blocker; blocker.id = "blocker"; blocker.name = "Blocker"; blocker.kind = 1; blocker.position = {0.75F,0,2.5F}; blocker.size = {0.25F,10,0.01F};
  composition.objects = {blocker}; const auto penumbra = render();
  // Dense midpoint quadrature plus independent segment/slab visibility. This
  // tests energy and partial occlusion, not a blurred hard-shadow image.
  const auto oracle = [&](bool shadow) {
    double sum = 0; constexpr int nx = 2048, ny = 128;
    for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
      const double px = 1+(x+0.5)*2/nx, py = -0.5+(y+0.5)/ny, r2 = px*px+py*py+9;
      bool blocked = false;
      if (shadow) {
        double enter = 0, leave = 1;
        const double origin[3]{0,0,4}, ray[3]{px,py,-3}, lo[3]{0.5,-10,2.49}, hi[3]{1,10,2.51};
        for (int axis = 0; axis < 3; ++axis) {
          if (std::abs(ray[axis]) < 1e-20) { if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) leave = -1; }
          else { const double a = (lo[axis]-origin[axis])/ray[axis], b = (hi[axis]-origin[axis])/ray[axis]; enter = std::max(enter,std::min(a,b)); leave = std::min(leave,std::max(a,b)); }
        }
        blocked = leave >= enter;
      }
      if (!blocked) sum += 9/(r2*r2);
    }
    return std::lround(127.5*encoded(40/M_PI*sum*2/(nx*ny)));
  };
  if (!require(rectangle.error.isEmpty() && penumbra.error.isEmpty() && std::abs(rectangle.image.pixelColor(64,64).red()-oracle(false)) <= 2 &&
      std::abs(penumbra.image.pixelColor(64,64).red()-oracle(true)) <= 2 && penumbra.image.pixelColor(64,64).red() > 20 && penumbra.image.pixelColor(64,64).red() < rectangle.image.pixelColor(64,64).red()-10,
      "Rectangle energy/received penumbra differs from independent area integration")) { std::cerr << " values=" << rectangle.image.pixelColor(64,64).red() << "/" << penumbra.image.pixelColor(64,64).red() << " oracle=" << oracle(false) << "/" << oracle(true); return 1; }
  composition.objects.clear(); composition.lights[0].rotation = {180,0,0};
  if (!require(render().image.pixelColor(64,64).red() == 0,"Rectangle emitted from its back face")) return 1;
  composition.lights[0] = light; composition.lights[0].vector = {0,0,0}; composition.objects = {blocker}; composition.objects[0].position = {}; composition.objects[0].size = {0.1F,0.1F,0.1F};
  if (!require(near(render().image,sphere.image,1),"Geometry behind a spherical emitting surface blocked its light")) return 1;
  composition.objects.clear(); composition.lights[0].vector = {0,0,4};
  if (!require(render().image.pixelColor(64,64).red() == 0,"A point inside a one-sided spherical source received invalid radiance")) return 1;
  // Measure the actual penumbra on a solid receiver, then compare a delta point.
  auto empty = std::make_shared<GaussianSceneData>(); auto behind = point(); behind.center_opacity[2] = -4; empty->points = {behind};
  GaussianSceneObject floor; floor.id = "floor"; floor.name = "Receiver"; floor.kind = 1; floor.position = {0,0,4}; floor.size = {1.5F,1.5F,0.1F}; floor.color = {1,1,1}; floor.roughness = 1;
  composition = GaussianSceneComposition{}; composition.ambient = 0; composition.light_samples = 256; composition.objects = {floor};
  light.kind = 2; light.vector = {2,0,1}; light.size = {2,1}; light.rotation = {}; light.intensity = 5; composition.lights = {light};
  const auto solid_render = [&] { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(empty,camera,true); };
  const auto solid_clear = solid_render(); blocker.position = {0.75F,0,2.5F}; blocker.size = {0.25F,10,0.01F}; composition.objects.push_back(blocker);
  const auto solid_shadow = solid_render(); int partial = 0;
  for (int x = 0; x < 129; ++x) { const double clear = linear(solid_clear.image.pixelColor(x,64).red()), shadow = linear(solid_shadow.image.pixelColor(x,64).red()); if (clear > 0.01 && shadow/clear > 0.05 && shadow/clear < 0.95) ++partial; }
  if (!require(solid_shadow.error.isEmpty() && partial > 30,"Finite emitter did not produce a geometric solid penumbra")) return 1;
  GaussianRenderOptions tiled; tiled.export_tile_edge = 64; tiled.lighting_batches_completed = std::make_shared<std::atomic_int>(0); const auto tiled_shadow = renderer->renderWithOptions(empty,camera,true,{},tiled);
  if (!require(tiled_shadow.error.isEmpty() && near(solid_shadow.image,tiled_shadow.image,0) && tiled.lighting_batches_completed->load() == tiled_shadow.metrics.value("lightingBatches").toInt() && tiled_shadow.metrics.value("lightingBatches").toInt() > solid_shadow.metrics.value("lightingBatches").toInt(),"Soft shadow sampling or cumulative progress changed at tile boundaries")) return 1;
  camera.photo.lens = {1.9F,0.1F,0,0,0,16}; composition.haze_density = 0.05F;
  const auto optical = solid_render(); const auto optical_tiles = renderer->renderWithOptions(empty,camera,true,{},tiled);
  if (!require(optical.error.isEmpty() && optical_tiles.error.isEmpty() && near(optical.image,optical_tiles.image,1),"Soft lights, aperture integration and haze disagree across tiles")) return 1;
  camera.photo = {}; composition.haze_density = 0; composition.lights[0].kind = 1; composition.lights[0].intensity = 10;
  composition.objects = {floor}; const auto point_clear = solid_render(); composition.objects.push_back(blocker); const auto point_shadow = solid_render(); int point_partial = 0;
  for (int x = 0; x < 129; ++x) { const double clear = linear(point_clear.image.pixelColor(x,64).red()), shadow = linear(point_shadow.image.pixelColor(x,64).red()); if (clear > 0.01 && shadow/clear > 0.05 && shadow/clear < 0.95) ++point_partial; }
  if (!require(point_partial == 0,"Original point light unexpectedly acquired a soft shadow")) return 1;
  composition.lights[0] = light; const auto parsed = gaussian_scene_composition(composition.description());
  if (!require(parsed && parsed->description() == composition.description(),"Finite light codec roundtrip failed")) return 1;
  QTemporaryDir temporary; const auto stored = save_gaussian_composition(temporary.path(),"artifact",composition,{});
  if (!require(stored.composition && load_gaussian_composition(temporary.path(),"artifact").composition->description() == composition.description(),"Finite lights were lost on reopen")) return 1;
  for (int invalid_samples : {0,1,63,2048}) { auto invalid = composition; invalid.light_samples = invalid_samples; if (!require(!gaussian_scene_composition(invalid.description()),"Invalid light sample count accepted")) return 1; }
  auto invalid = composition; invalid.lights[0].size.setX(0); if (!require(!gaussian_scene_composition(invalid.description()),"Zero-area emitter accepted")) return 1;
  // Only the last Gaussian contributes: its tilted covariance normal lies
  // beyond the 65,536-point dispatch boundary. The exact sphere irradiance
  // gains a cos(45 degrees) factor, independently of dispatch layout.
  auto banded = std::make_shared<GaussianSceneData>(); banded->points.assign(65537,point());
  for (auto& p : banded->points) p.center_opacity[3] = 0;
  auto& last = banded->points.back(); last.center_opacity[3] = 0.5F; last.covariance_a = {0.02005F,0,0.01995F,0.04F}; last.covariance_b = {0,0.02005F,0,0};
  auto band_composition = GaussianSceneComposition{}; band_composition.ambient = 0; band_composition.capture_lighting = 1; band_composition.light_samples = 1024;
  GaussianSceneLight band_light; band_light.id = "band-light"; band_light.name = "Sphere"; band_light.kind = 3; band_light.vector = {}; band_light.radius = 1; band_light.intensity = 4; band_composition.lights = {band_light};
  camera.composition = std::make_shared<GaussianSceneComposition>(band_composition);
  const auto band_frame = renderer->render(banded,camera,true);
  if (!require(band_frame.error.isEmpty() && std::abs(band_frame.image.pixelColor(64,64).red()-std::lround(127.5*encoded(0.25*std::sqrt(0.5)))) <= 1,"Batched covariance normals or irradiance did not reach the last Gaussian")) return 1;
  // Cancellation is triggered by an observed completed batch, not a guessed
  // sleep. No partially integrated frame may be published as a photograph.
  auto many = std::make_shared<GaussianSceneData>(); many->points.assign(4096,point()); composition.objects.clear(); composition.capture_lighting = 1; composition.light_samples = 1024;
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto canceled = std::make_shared<std::atomic_bool>(false); const auto batches = std::make_shared<std::atomic_int>(0);
  std::jthread canceler([&] { QElapsedTimer timer; timer.start(); while (batches->load() == 0 && timer.elapsed() < 5000) std::this_thread::yield(); canceled->store(true); });
  GaussianRenderOptions cancel_options; cancel_options.canceled = canceled; cancel_options.lighting_batches_completed = batches;
  const auto interrupted = renderer->renderWithOptions(many,camera,true,{},cancel_options); canceler.join();
  if (!require(batches->load() > 0 && interrupted.canceled && interrupted.image.isNull() && interrupted.native_texture == 0,"Canceled soft refinement published an incomplete image")) return 1;
  if (!proof.isEmpty()) {
    for (const auto& entry : {std::pair<QString,QImage>{"sphere-capture.png",sphere.image},{"rectangle-capture-shadow.png",penumbra.image},{"solid-clear.png",solid_clear.image},{"solid-penumbra.png",solid_shadow.image},{"solid-point-shadow.png",point_shadow.image},{"soft-aperture-haze.png",optical.image}})
      if (!entry.second.save(QDir(proof).filePath(entry.first))) return 2;
    QFile receipt(QDir(proof).filePath("receipt.json"));
    const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"scope","independent synthetic finite-light renderer oracles"},{"solidPartialPixels",partial},{"pointPartialPixels",point_partial},{"rectangleExpected",static_cast<int>(oracle(false))},{"rectangleActual",rectangle.image.pixelColor(64,64).red()},{"shadowExpected",static_cast<int>(oracle(true))},{"shadowActual",penumbra.image.pixelColor(64,64).red()},{"cancellationObservedBatches",batches->load()},{"solidMetrics",solid_shadow.metrics}}).toJson();
    if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly) || receipt.write(bytes) != bytes.size()) return 2;
  }
  std::cout << "Soft lights: analytic sphere/sun energy, rectangle quadrature/one-sidedness, captured and solid penumbrae, source-distance visibility, draft/final quality, exact tiling, aperture/haze parity, persistence and observed-batch cancellation pass.\n";
  return 0;
}
