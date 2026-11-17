#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <thread>

using namespace photo_editor::desktop;
namespace {
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
double encode(double c) { return c <= 0.0031308 ? 12.92*c : 1.055*std::pow(c,1/2.4)-0.055; }
GaussianPoint point(QVector3D position,QVector3D color,float opacity,float sigma = 10000) {
  GaussianPoint p; p.center_opacity = {position.x(),position.y(),position.z(),opacity};
  p.covariance_a = {sigma*sigma,0,0,sigma*sigma}; p.covariance_b = {0,sigma*sigma,0,0};
  for (int c = 0; c < 3; ++c) p.sh[static_cast<size_t>(c)] = (color[c]-0.5F)/0.2820947918F;
  return p;
}
QVector3D captured_integral(QVector3D delta,QVector3D color,double opacity,double sigma) {
  constexpr int latitudes = 512, longitudes = 256; constexpr double pi = 3.14159265358979323846;
  double result[3]{}; const double squared = delta.lengthSquared();
  for (int t = 0; t < latitudes; ++t) {
    const double u = (t+0.5)/latitudes, sine = std::sqrt(1-u*u);
    for (int p = 0; p < longitudes; ++p) {
      const double phi = (p+0.5)*2*pi/longitudes;
      const double peak = std::max(0.0,delta.x()*sine*std::cos(phi)+delta.y()*sine*std::sin(phi)-delta.z()*u);
      const double alpha = std::min(0.999,opacity*std::exp(-0.5*std::max(0.0,squared-peak*peak)/(sigma*sigma)));
      if (alpha < 1.0/255) continue;
      for (int c = 0; c < 3; ++c) result[c] += 2*u*decode(alpha*color[c])/(latitudes*longitudes);
    }
  }
  return {static_cast<float>(encode(0.96*result[0])),static_cast<float>(encode(0.96*result[1])),static_cast<float>(encode(0.96*result[2]))};
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); const auto args = app.arguments(); const auto at = args.indexOf("--proof-dir");
  const auto proof = at < 0 ? QString{} : args.value(at+1);
  if (at >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto fail = [](const char* reason) { std::cerr << "Spatial diffuse: " << reason << '\n'; return 1; };
  GaussianSceneComposition composition; composition.ambient = 0; composition.environment_diffuse = true; composition.spatial_environment = true; composition.light_samples = 1024;
  GaussianSceneObject receiver; receiver.id = "receiver"; receiver.name = "Receiver"; receiver.kind = 1;
  receiver.position = {}; receiver.size = {3,3,0.01F}; receiver.color = {1,1,1}; receiver.roughness = 1; receiver.metallic = 0;
  composition.objects = {receiver};
  const auto parsed = gaussian_scene_composition(composition.description());
  if (!parsed || parsed->description() != composition.description() || parsed->needsProbe() || !parsed->softLighting() || composition.description().value("schema") != "looksy-scene-composition-v12") return fail("v12 codec / pipeline requirements");
  for (const auto& invalid : {QVariant(true),QVariant(1),QVariant("unknown")}) {
    auto broken = composition.description(); broken["illuminationMode"] = invalid;
    if (gaussian_scene_composition(broken)) return fail("invalid illumination mode accepted");
  }
  auto probe_description = composition.description(); probe_description["illuminationMode"] = "probe";
  const auto probe = gaussian_scene_composition(probe_description);
  if (!probe || probe->spatial_environment || !probe->needsProbe() || probe->description().contains("illuminationMode")) return fail("probe canonicalization");
  auto independent = composition; independent.reflections = true;
  if (!independent.needsProbe()) return fail("probe specular lost its probe with spatial diffuse");
  independent.spatial_reflections = true;
  const auto both = gaussian_scene_composition(independent.description());
  if (!both || !both->spatial_environment || !both->spatial_reflections || both->needsProbe()) return fail("independent spatial methods");
  independent.environment_diffuse = false; independent.reflections = false;
  const auto disabled = gaussian_scene_composition(independent.description());
  if (!disabled || !disabled->spatial_environment || !disabled->spatial_reflections || disabled->needsProbe() || disabled->softLighting()) return fail("disabled methods lost their selection or still trace");
  QTemporaryDir project;
  const auto stored = save_gaussian_composition(project.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!stored.composition || !reopened.composition || !reopened.composition->spatial_environment) return fail("persistence");
  GaussianCamera camera; camera.image_size = {1,1}; camera.fx = camera.fy = 1; camera.cx = camera.cy = 0.5F;
  camera.position = {0,0,-3}; camera.forward = {0,0,1}; camera.right = {1,0,0}; camera.down = {0,1,0};
  auto scene = std::make_shared<GaussianSceneData>(); auto renderer = make_gaussian_scene_renderer();
  const auto render = [&](std::optional<double> time = {}) { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true,time); };
  QVariantList cases; int maximum_error = 0;
  const auto check = [&](QVector3D expected,const char* name,std::optional<double> time = {},bool spatial = true) {
    const auto frame = render(time); if (!frame.error.isEmpty() || frame.image.isNull()) { std::cerr << frame.error.toStdString() << '\n'; return false; }
    const auto actual = frame.image.pixelColor(frame.image.width()/2,frame.image.height()/2); const int values[]{actual.red(),actual.green(),actual.blue()};
    int error = 0; for (int c = 0; c < 3; ++c) error = std::max(error,std::abs(values[c]-qRound(255*expected[c])));
    maximum_error = std::max(maximum_error,error);
    if (spatial && (frame.metrics.value("environmentLightingModel") != "surface-cosine-spatial-gaussian-v1" || frame.metrics.contains("environmentProbeOrigin") || frame.metrics.contains("scanProbeMs"))) return false;
    cases.append(QVariantMap{{"case",name},{"maximumChannelError",error},{"metrics",frame.metrics}});
    if (!proof.isEmpty() && !frame.image.save(proof+"/"+name+".png")) return false;
    if (error > 2) { std::cerr << name << " RGB=" << values[0] << ',' << values[1] << ',' << values[2] << " error=" << error << '\n'; return false; }
    return true;
  };
  const auto uniform = [](double color,double opacity,double strength = 1) { return static_cast<float>(encode(0.96*decode(color*opacity)*strength)); };
  scene->points = {point({0,0,-10},{0.5F,0.5F,0.5F},0.99F)};
  const float white = uniform(0.5,0.99);
  if (!check({white,white,white},"captured-white-furnace")) return fail("cosine captured-radiance integral");
  composition.reflections = true; composition.spatial_reflections = true;
  double specular_integral = 0; constexpr int furnace_steps = 8192;
  for (int i = 0; i < furnace_steps; ++i) {
    const double u = (i+0.5)/furnace_steps, vh = std::sqrt((1+u)/2), fresnel = 0.04+0.96*std::pow(1-vh,5);
    specular_integral += fresnel*u/(1+u)/furnace_steps;
  }
  const float combined = static_cast<float>(encode(decode(0.495)*(0.96+specular_integral)));
  if (!check({combined,combined,combined},"combined-spatial-white-furnace")) return fail("combined specular/diffuse energy");
  composition.spatial_reflections = false;
  if (!check({0.495F,0.495F,0.495F},"probe-specular-spatial-diffuse",{},false)) return fail("independent runtime methods");
  const auto mixed = render();
  if (mixed.metrics.value("reflectionModel") != "object-center-linear-half-capture-probe-v1" ||
      mixed.metrics.value("environmentLightingModel") != "surface-cosine-spatial-gaussian-v1" ||
      !mixed.metrics.contains("reflectionProbeOrigin") || mixed.metrics.contains("environmentProbeOrigin")) return fail("mixed-method provenance");
  composition.reflections = false;
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point({0,0,-10},{0.5F,0.5F,0.5F},1)};
  const float brush = uniform(0.5,0.999);
  if (!check({brush,brush,brush},"brush-alpha-cap")) return fail("Brush cap");
  composition.alpha_model = GaussianAlphaModel::Inria99;
  if (!check({white,white,white},"inria-alpha-cap")) return fail("original cap");
  composition.alpha_model = GaussianAlphaModel::Brush999; composition.environment_strength = 2;
  const float strong = uniform(0.5,0.999,2);
  if (!check({strong,strong,strong},"linear-strength")) return fail("strength applied outside linear radiance");
  composition.environment_strength = 0;
  if (!check({0,0,0},"disabled-strength",{},false)) return fail("zero-strength bypass");
  composition.environment_strength = 1;
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point({0,0,-4},{0.8F,0.2F,0.1F},0.8F,1)};
  auto distant = receiver; distant.id = "distant"; distant.name = "Distant anchor"; distant.position = {1000,1000,1000}; distant.size = {0.1F,0.1F,0.1F};
  composition.objects = {distant,receiver};
  if (!check(captured_integral({0,0,-3.99F},{0.8F,0.2F,0.1F},0.8,1),"local-captured-radiance")) return fail("finite captured field / surface origin");
  camera.position.setX(2); composition.objects[1].position.setX(2);
  if (!check(captured_integral({-2,0,-3.99F},{0.8F,0.2F,0.1F},0.8,1),"translated-captured-radiance")) return fail("spatially varying captured integral");
  composition.spatial_environment = false;
  if (!check({0,0,0},"distant-probe-misses-captured-field",{},false)) return fail("local field versus shared probe");
  camera.position.setX(0); composition.spatial_environment = true; composition.objects = {receiver};
  scene = std::make_shared<GaussianSceneData>(); scene->sh_degree = 1;
  auto directional = point({0,0,-10},{0.5F,0.5F,0.5F},0.99F); directional.sh[6] = 0.3F; scene->points = {directional};
  // Independent midpoint integration over cos(theta), density 2*cos(theta).
  // It integrates nonlinear captured sRGB decoding, rather than averaging SH.
  double red = 0; constexpr int integration_steps = 8192;
  for (int i = 0; i < integration_steps; ++i) { const double u = (i+0.5)/integration_steps; red += 2*u*decode(0.99*(0.5-0.4886025119*0.3*u))/integration_steps; }
  if (!check({static_cast<float>(encode(0.96*red)),white,white},"directional-sh-cosine-integral")) return fail("spatial SH hemisphere orientation / PDF");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point({0,0,-10},{0.5F,0.5F,0.5F},0.99F)};
  scene->time_origin_seconds = 100; scene->local_time_range = {0,2}; scene->temporal_sh_degree = 1;
  GaussianTemporalPoint motion; motion.velocity_time = {0.5F,0,0,0}; motion.normalization[0] = 1;
  motion.covariance_factor = {10000,0,0,0,0,10000,0,0,0,0,10000,0}; scene->temporal = {motion};
  GaussianTemporalAppearance appearance; appearance.sh[1] = 0.1F/0.2820947918F; scene->temporal_appearance = {appearance};
  const float temporal_green = uniform(0.6,0.99);
  if (!check({white,temporal_green,white},"temporal-start",100)) return fail("temporal start");
  const auto initial = render(100); const float faded = uniform(0.5,0.99*std::exp(-0.125));
  if (!check({faded,faded,faded},"temporal-faded",100.5) || !check({white,temporal_green,white},"temporal-revisit",100) || render(100).image != initial.image) return fail("temporal appearance / cache revisit");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point({0,0,-100},{0,0,0},0)};
  auto emitter = receiver; emitter.id = "emission"; emitter.name = "Emissive panel"; emitter.position = {0,0,-4}; emitter.size = {10000,10000,0.01F}; emitter.color = {0.5F,0.5F,0.5F}; emitter.emission = 1;
  composition.objects = {receiver,emitter}; const float emitted = static_cast<float>(encode(0.96*decode(0.5)));
  if (!check({emitted,emitted,emitted},"authored-emission-indirect")) return fail("emissive indirect surface");
  auto anchor = receiver; anchor.id = "anchor"; anchor.name = "Distant anchor"; anchor.position = {1000,1000,1000}; anchor.size = {0.1F,0.1F,0.1F};
  composition.objects = {anchor,receiver,emitter};
  if (!check({emitted,emitted,emitted},"surface-independent-of-first-object")) return fail("single-anchor dependence");
  composition.spatial_environment = false;
  if (!check({0,0,0},"probe-misses-authored-emission",{},false)) return fail("probe A/B");
  composition.spatial_environment = true;
  auto small_receiver = receiver; small_receiver.kind = 0; small_receiver.size = {0.01F,0.01F,0.01F};
  auto lit_panel = emitter; lit_panel.emission = 0; lit_panel.color = {0.5F,0.25F,0.125F};
  composition.objects = {small_receiver,lit_panel};
  GaussianSceneLight sun; sun.id = "sun"; sun.name = "Back-facing key"; sun.vector = {0,0,1}; sun.intensity = 1;
  composition.lights = {sun}; QVector3D bounced; constexpr double pi = 3.14159265358979323846;
  for (int c = 0; c < 3; ++c) {
    double value = 0;
    for (int i = 0; i < integration_steps; ++i) {
      const double u = (i+0.5)/integration_steps, radial = 3.98*std::sqrt(1-u*u)/u;
      if (radial < 0.01) continue; // The tiny receiver's real shadow on the panel.
      const double vh = std::sqrt((1+u)/2), fresnel = 0.04+0.96*std::pow(1-vh,5);
      const double brdf = decode(lit_panel.color[c])*(1-fresnel)/pi+fresnel/(2*pi*(1+u));
      value += 2*u*brdf/integration_steps;
    }
    bounced[c] = static_cast<float>(encode(0.96*value));
  }
  if (!check(bounced,"directional-light-secondary-surface")) return fail("direct-lit secondary surface / indirect bounce");
  composition.lights.clear(); composition.objects = {receiver,emitter}; composition.haze_density = 0.05F; composition.haze_color = {0.1F,0.1F,0.1F};
  const double fog = decode(0.1); double incoming = 0;
  for (int i = 0; i < integration_steps; ++i) { const double u = (i+0.5)/integration_steps; incoming += 2*u*(fog+(decode(0.5)-fog)*std::exp(-0.05*3.98/u))/integration_steps; }
  const float hazed = static_cast<float>(encode(fog+(0.96*incoming-fog)*std::exp(-0.05*2.99)));
  if (!check({hazed,hazed,hazed},"two-segment-atmosphere")) return fail("diffuse incoming and primary atmosphere");
  composition.haze_density = 0; composition.objects = {receiver};
  GaussianSceneLight source; source.id = "source"; source.name = "Finite source"; source.kind = 3; source.vector = {0,0,-5}; source.radius = 1.5F; source.intensity = 2;
  composition.lights = {source}; composition.environment_diffuse = false; const auto direct = render();
  composition.environment_diffuse = true; const auto spatial = render();
  if (!direct.error.isEmpty() || !spatial.error.isEmpty() || direct.image.isNull() || direct.image != spatial.image) return fail("finite-source diffuse counted twice");
  cases.append(QVariantMap{{"case","finite-source-no-double-counting"},{"maximumChannelError",0},{"metrics",spatial.metrics}});
  composition.lights.clear(); composition.light_samples = 32; composition.objects = {receiver,emitter};
  camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  const auto full = render(); GaussianRenderOptions tiles; tiles.export_tile_edge = 32;
  const auto tiled = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!full.error.isEmpty() || !tiled.error.isEmpty() || full.image != tiled.image) return fail("exact tiling");
  camera.photo.lens = {3.0F,0.02F,0,0,0,16}; composition.haze_density = 0.05F;
  const auto optical = render(); const auto optical_tiles = renderer->renderWithOptions(scene,camera,true,{},tiles);
  if (!optical.error.isEmpty() || !optical_tiles.error.isEmpty() || optical.image != optical_tiles.image) return fail("aperture / haze tiling");
  camera.photo = {}; camera = camera.resized({513,513}); camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto canceled = std::make_shared<std::atomic_bool>(false); const auto batches = std::make_shared<std::atomic_int>(0); std::atomic_bool observed{false};
  std::jthread stop([&] { QElapsedTimer timer; timer.start(); while (batches->load() == 0 && timer.elapsed() < 5000) std::this_thread::yield(); observed.store(batches->load() > 0); canceled->store(true); });
  GaussianRenderOptions options; options.canceled = canceled; options.lighting_batches_completed = batches;
  const auto interrupted = renderer->renderWithOptions(scene,camera,true,{},options); stop.join();
  if (!observed.load() || !interrupted.canceled || !interrupted.image.isNull() || interrupted.native_texture) return fail("observed-batch cancellation published partial output");
  QVariantMap demo;
  if (!proof.isEmpty()) {
    // A real renderer A/B makes the authored-emission transfer visible. The
    // source is an object in both frames; only the illumination method changes.
    camera = {}; camera.image_size = {512,384}; camera.fx = camera.fy = 420; camera.cx = 256; camera.cy = 192;
    camera.position = {1.8F,-1.1F,-3.5F}; camera.forward = (QVector3D(0,0.1F,0)-camera.position).normalized();
    camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized(); camera.down = QVector3D::crossProduct(camera.forward,camera.right).normalized();
    composition = {}; composition.ambient = 0.02F; composition.environment_diffuse = true; composition.light_samples = 128;
    auto ball = receiver; ball.kind = 0; ball.position = {0,0.1F,0}; ball.size = {0.5F,0.5F,0.5F}; ball.color = {0.8F,0.8F,0.8F}; ball.roughness = 0.65F;
    auto ground = receiver; ground.id = "ground"; ground.name = "Ground"; ground.position = {0,0.62F,0}; ground.size = {2,0.02F,2}; ground.color = {0.6F,0.6F,0.6F};
    auto blue = emitter; blue.id = "blue-source"; blue.name = "Blue emissive box"; blue.position = {-0.95F,-0.1F,-0.15F}; blue.size = {0.2F,0.4F,0.2F}; blue.color = {0.05F,0.12F,1}; blue.emission = 6;
    composition.objects = {ball,ground,blue}; sun.vector = {-0.4F,-0.7F,-1}; sun.intensity = 1; composition.lights = {sun};
    scene = std::make_shared<GaussianSceneData>(); scene->points = {point({0,0,-100},{0,0,0},0)};
    const auto before = render(); composition.spatial_environment = true; const auto after = render();
    if (!before.error.isEmpty() || !after.error.isEmpty() || before.image.isNull() || before.image.size() != after.image.size()) return fail("emission demo rendering");
    int blue_transfer_pixels = 0;
    for (int y = 0; y < after.image.height(); ++y) for (int x = 0; x < after.image.width(); ++x) {
      const auto a = before.image.pixelColor(x,y), b = after.image.pixelColor(x,y);
      const int gain = b.blue()-a.blue(); if (gain >= 8 && b.red()-a.red() < gain/2 && a.blue() < 230) ++blue_transfer_pixels;
    }
    if (blue_transfer_pixels < 100 || !before.image.save(proof+"/emission-demo-probe.png") || !after.image.save(proof+"/emission-demo-spatial.png")) return fail("visible indirect blue transfer");
    demo = {{"blueTransferPixels",blue_transfer_pixels},{"camera",camera.description()},{"composition",composition.description()},{"metrics",after.metrics},
        {"scope","synthetic native renderer A/B; authored source unchanged, illumination method differs"}};
  }
  const QVariantMap receipt{{"passed",true},{"cases",cases},{"maximumChannelError",maximum_error},{"exactTiling",true},{"exactApertureHazeTiling",true},{"cancellationObservedBatches",batches->load()},
      {"emissionDemo",demo},
      {"scope","independent spatial diffuse radiance, emission, temporal, finite-source and publication oracles; not complete multi-bounce or photographic parity"}};
  if (!proof.isEmpty()) { QFile file(proof+"/receipt.json"); const auto data = QJsonDocument::fromVariant(receipt).toJson(); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return 2; }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
}
