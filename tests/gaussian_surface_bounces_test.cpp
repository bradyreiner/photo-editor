#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <iostream>
#include <thread>

using namespace photo_editor::desktop;
namespace {
constexpr double pi = 3.14159265358979323846;
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
double encode(double c) { return c <= 0.0031308 ? 12.92*c : 1.055*std::pow(c,1/2.4)-0.055; }
GaussianPoint background(float opacity = 0,float color = 0) {
  GaussianPoint p; p.center_opacity = {0,0,-100,opacity};
  p.covariance_a = {1e12F,0,0,1e12F}; p.covariance_b = {0,1e12F,0,0};
  for (int c = 0; c < 3; ++c) p.sh[static_cast<size_t>(c)] = (color-0.5F)/0.2820947918F;
  return p;
}
// Independent angular quadrature of the BRDF, without sampling a GGX normal
// or reproducing the renderer's lobe choice and random sequences.
struct CavityOperator {
  static constexpr int bins = 128, azimuths = 256;
  std::vector<double> matrix = std::vector<double>(bins*bins), primary = std::vector<double>(bins);
  CavityOperator() {
    const auto kernel = [](double v,double u) {
      const double lv = 0.5*(1/v-1), lu = 0.5*(1/u-1); // roughness=1 Smith lambda
      double specular = 0;
      for (int k = 0; k < azimuths; ++k) {
        const double phi = (k+0.5)*2*pi/azimuths;
        const double dot = v*u+std::sqrt((1-v*v)*(1-u*u))*std::cos(phi);
        const double vh = std::sqrt(std::max(0.0,(1+dot)/2));
        const double fresnel = 0.04+0.96*std::pow(1-vh,5);
        specular += fresnel/(pi*(1+lv+lu)*4*v)*2*pi/azimuths;
      }
      return 0.96*(1-std::pow(1-v,5))*2*u+specular;
    };
    for (int v = 0; v < bins; ++v) for (int u = 0; u < bins; ++u)
      matrix[static_cast<size_t>(v*bins+u)] = kernel((v+0.5)/bins,(u+0.5)/bins)/bins;
    for (int u = 0; u < bins; ++u) primary[static_cast<size_t>(u)] = kernel(1,(u+0.5)/bins)/bins;
  }
  std::vector<double> apply(const std::vector<double>& input,double emission) const {
    std::vector<double> result(bins,emission);
    for (int v = 0; v < bins; ++v) for (int u = 0; u < bins; ++u)
      result[static_cast<size_t>(v)] += matrix[static_cast<size_t>(v*bins+u)]*input[static_cast<size_t>(u)];
    return result;
  }
  double radiance(int depth,double emission) const {
    std::vector<double> root(bins,emission), other(bins,0);
    for (int i = 1; i < depth; ++i) { const auto next_root = apply(other,emission); other = apply(root,0); root = next_root; }
    double value = emission;
    for (int u = 0; u < bins; ++u) value += primary[static_cast<size_t>(u)]*other[static_cast<size_t>(u)];
    return value;
  }
};
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); const auto args = app.arguments(); const auto at = args.indexOf("--proof-dir");
  const auto proof = at < 0 ? QString{} : args.value(at+1);
  if (at >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto fail = [](const char* reason) { std::cerr << "Surface bounces: " << reason << '\n'; return 1; };
  GaussianSceneComposition composition; composition.ambient = 0; composition.environment_diffuse = true; composition.spatial_environment = true;
  composition.light_samples = 1024; composition.surface_bounces = 4;
  GaussianSceneObject receiver; receiver.id = "receiver"; receiver.name = "Receiver"; receiver.kind = 1;
  receiver.position = {}; receiver.size = {10000,10000,0.01F}; receiver.color = {1,1,1}; receiver.roughness = 1; receiver.emission = 0.1F;
  auto other = receiver; other.id = "other"; other.name = "Facing panel"; other.position = {0,0,-4}; other.emission = 0;
  composition.objects = {receiver,other};
  const auto parsed = gaussian_scene_composition(composition.description());
  if (!parsed || parsed->surface_bounces != 4 || parsed->description() != composition.description() || composition.description().value("schema") != "looksy-scene-composition-v13") return fail("v13 codec");
  for (const auto& value : {QVariant(-1),QVariant(0),QVariant(9),QVariant(1.5),QVariant(true),QVariant("4")}) {
    auto broken = composition.description(); broken["surfaceBounces"] = value;
    if (gaussian_scene_composition(broken)) return fail("invalid depth accepted");
  }
  auto single = composition.description(); single["surfaceBounces"] = 1;
  const auto canonical = gaussian_scene_composition(single);
  if (!canonical || canonical->description().value("schema") != "looksy-scene-composition-v12" || canonical->description().contains("surfaceBounces")) return fail("single-depth canonicalization");
  QTemporaryDir project;
  const auto saved = save_gaussian_composition(project.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!saved.composition || !reopened.composition || reopened.composition->description() != composition.description()) return fail("depth persistence");
  GaussianCamera camera; camera.image_size = {1,1}; camera.fx = camera.fy = 1; camera.cx = camera.cy = 0.5F;
  camera.position = {0,0,-3}; camera.forward = {0,0,1}; camera.right = {1,0,0}; camera.down = {0,1,0};
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {background()}; auto renderer = make_gaussian_scene_renderer();
  const auto render = [&](const GaussianRenderOptions& options = GaussianRenderOptions{}) {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->renderWithOptions(scene,camera,true,{},options);
  };
  QVariantList cases; int maximum_error = 0;
  const auto check = [&](double linear,const char* name) {
    const auto frame = render();
    if (!frame.error.isEmpty() || frame.image.isNull()) { std::cerr << frame.error.toStdString() << '\n'; return false; }
    const auto actual = frame.image.pixelColor(0,0); const int expected = qRound(255*encode(linear));
    const int error = std::max({std::abs(actual.red()-expected),std::abs(actual.green()-expected),std::abs(actual.blue()-expected)});
    maximum_error = std::max(maximum_error,error);
    if (composition.surface_bounces > 1 && (frame.metrics.value("surfaceBounces").toInt() != composition.surface_bounces || frame.metrics.value("surfaceTransportModel") != "bounded-surface-paths-ggx-cosine-v1")) return false;
    cases.append(QVariantMap{{"case",name},{"expectedLinear",linear},{"expectedChannel",expected},{"actualChannel",actual.red()},{"maximumChannelError",error},{"metrics",frame.metrics}});
    if (!proof.isEmpty() && !frame.image.save(proof+"/"+name+".png")) return false;
    if (error > 2) { std::cerr << name << " expected=" << expected << " actual=" << actual.red() << " error=" << error << '\n'; return false; }
    return true;
  };
  constexpr double emission = 0.1, mean_diffuse = 0.96*20/21;
  for (int depth : {1,2,4,8}) {
    composition.surface_bounces = depth; double value = emission;
    for (int hit = 2; hit <= depth; hit += 2) value += emission*0.96*std::pow(mean_diffuse,hit-1);
    const auto name = QStringLiteral("diffuse-cavity-depth-%1").arg(depth).toLatin1();
    if (!check(value,name.constData())) return fail("independent diffuse cavity energy");
  }
  composition.surface_bounces = 2; composition.haze_density = 0.1F; composition.haze_color = {0,0,0};
  double first_segment = 0, second_segment = 0; constexpr int steps = 32768; constexpr double distance = 3.98;
  for (int i = 0; i < steps; ++i) {
    const double u = (i+0.5)/steps, attenuation = std::exp(-0.1*distance/u);
    first_segment += 2*u*0.96*(1-std::pow(1-u,5))*attenuation/steps; second_segment += 2*u*attenuation/steps;
  }
  if (!check(emission*(1+0.96*first_segment*second_segment)*std::exp(-0.1*2.99),"two-bounce-segment-haze")) return fail("per-segment haze");
  composition.haze_density = 0; composition.haze_color = {0.7F,0.8F,1};
  scene = std::make_shared<GaussianSceneData>(); scene->points = {background(0.25F,0.2F)};
  double captured = 0;
  for (int i = 0; i < steps; ++i) {
    const double u = (i+0.5)/steps, weight = 0.96*(1-std::pow(1-u,5));
    const double far = 0.25*decode(0.2)+0.75*emission;
    captured += 2*u*(0.25*decode(0.2)+0.75*weight*far)/steps;
  }
  if (!check(emission+0.96*captured,"two-bounce-captured-opacity")) return fail("linear captured composition");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {background()};
  composition.environment_diffuse = false; composition.reflections = true; composition.spatial_reflections = true;
  composition.objects[0].color = {0.5F,0.5F,0.5F}; composition.objects[0].metallic = 1; composition.objects[0].roughness = 0.04F; composition.objects[0].emission = 1;
  composition.objects[1].metallic = 1; composition.objects[1].roughness = 0.04F;
  const double mirror_emission = decode(0.5);
  for (int depth : {1,2,4,8}) {
    composition.surface_bounces = depth; double value = mirror_emission;
    for (int hit = 2; hit <= depth; hit += 2) value += mirror_emission*std::pow(mirror_emission,hit/2);
    const auto name = QStringLiteral("mirror-cavity-depth-%1").arg(depth).toLatin1();
    if (!check(value,name.constData())) return fail("repeated mirror energy");
  }
  composition.objects[0].color = {1,1,1}; composition.objects[0].emission = 0;
  composition.objects[1].rotation = {0,45,0};
  GaussianSceneLight source; source.id = "source"; source.name = "Reflected source"; source.vector = {4,0,-4}; source.intensity = 0.1F;
  source.radius = 0.5F; source.size = {2,2}; source.rotation = {0,-90,0};
  for (int kind : {2,3}) {
    source.kind = kind; composition.lights = {source};
    for (int depth : {2,4}) {
      composition.surface_bounces = depth; const auto name = QStringLiteral("finite-source-%1-depth-%2").arg(kind).arg(depth).toLatin1();
      if (!check(0.1,name.constData())) return fail("finite-source reflected energy / double counting");
    }
  }
  composition.lights.clear();
  composition.objects = {receiver,other}; composition.objects[0].emission = 0.06F;
  composition.environment_diffuse = true; const CavityOperator integral;
  for (int depth : {2,4,8}) {
    composition.surface_bounces = depth; const auto name = QStringLiteral("mixed-cavity-depth-%1").arg(depth).toLatin1();
    if (!check(integral.radiance(depth,0.06),name.constData())) return fail("independent mixed-BRDF angular quadrature");
  }
  composition.objects = {receiver}; composition.objects[0].emission = 0;
  composition.reflections = false; composition.surface_bounces = 8;
  for (int kind : {2,3}) {
    source.kind = kind; source.vector = {0,0,-4}; source.rotation = {}; source.intensity = 1;
    composition.lights = {source}; composition.environment_diffuse = false; const auto direct = render();
    composition.environment_diffuse = true; const auto indirect = render();
    if (!direct.error.isEmpty() || direct.image.isNull() || direct.image != indirect.image) return fail("finite diffuse source counted twice");
    cases.append(QVariantMap{{"case",QStringLiteral("finite-source-%1-diffuse-partition").arg(kind)},{"maximumChannelError",0},{"metrics",indirect.metrics}});
  }
  composition.lights.clear(); composition.objects = {receiver,other}; composition.objects[0].emission = 0.06F;
  // The chosen depth is retained even when both methods are disabled, and the
  // renderer returns to the ordinary non-path pipeline without stale state.
  composition.reflections = false; composition.environment_diffuse = false;
  const auto disabled = gaussian_scene_composition(composition.description()); const auto off = render();
  if (!disabled || disabled->surface_bounces != 8 || off.metrics.contains("surfaceTransportModel")) return fail("disabled depth / stale pipeline");
  composition.environment_diffuse = true; composition.reflections = true; composition.surface_bounces = 4;
  camera.image_size = {65,65}; camera.fx = camera.fy = 50; camera.cx = camera.cy = 32.5F;
  const auto full = render(); GaussianRenderOptions tiles; tiles.export_tile_edge = 32; const auto tiled = render(tiles);
  if (!full.error.isEmpty() || full.image.isNull() || full.image != tiled.image) return fail("exact tiled multi-surface transport");
  auto cancel = std::make_shared<std::atomic_bool>(false); auto batches = std::make_shared<std::atomic_int>(0); GaussianRenderOptions options; options.canceled = cancel; options.lighting_batches_completed = batches;
  std::jthread observer([cancel,batches](std::stop_token stop) { while (!stop.stop_requested() && batches->load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1)); if (!stop.stop_requested()) cancel->store(true); });
  const auto canceled = render(options); observer.request_stop(); observer.join();
  if (!cancel->load() || !canceled.canceled || !canceled.image.isNull() || canceled.native_texture != 0) return fail("GPU-batch cancellation");
  QVariantMap demo;
  if (!proof.isEmpty()) {
    composition = {}; composition.ambient = 0.02F; composition.environment_diffuse = true; composition.spatial_environment = true; composition.light_samples = 128;
    GaussianSceneObject sphere; sphere.id = "receiver"; sphere.name = "Receiver"; sphere.position = {0,0.1F,0}; sphere.color = {0.8F,0.8F,0.8F}; sphere.roughness = 0.65F;
    GaussianSceneObject ground; ground.id = "ground"; ground.name = "Ground"; ground.kind = 1; ground.position = {0,0.62F,0}; ground.size = {2,0.02F,2}; ground.color = {0.6F,0.6F,0.6F}; ground.roughness = 1;
    auto blue = ground; blue.id = "blue-source"; blue.name = "Blue emissive box"; blue.position = {-0.95F,-0.1F,-0.15F}; blue.size = {0.2F,0.4F,0.2F}; blue.color = {0.05F,0.12F,1}; blue.emission = 6;
    composition.objects = {sphere,ground,blue}; GaussianSceneLight key; key.id = "sun"; key.name = "Key"; key.intensity = 1; key.vector = {-0.4F,-0.7F,-1}; composition.lights = {key};
    camera = {}; camera.image_size = {512,384}; camera.fx = camera.fy = 420; camera.cx = 256; camera.cy = 192;
    camera.position = {1.8F,-1.1F,-3.5F}; camera.forward = (sphere.position-camera.position).normalized();
    camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized(); camera.down = QVector3D::crossProduct(camera.forward,camera.right).normalized();
    composition.surface_bounces = 1; const auto first = render(); composition.surface_bounces = 4; const auto fourth = render();
    if (!first.error.isEmpty() || !fourth.error.isEmpty() || first.image.isNull() || fourth.image.isNull() ||
        !first.image.save(proof+"/emission-depth-1.png") || !fourth.image.save(proof+"/emission-depth-4.png")) return fail("multi-bounce demonstration");
    int transfer = 0;
    for (int y = 0; y < first.image.height(); ++y) for (int x = 0; x < first.image.width(); ++x) {
      const auto before = first.image.pixelColor(x,y), after = fourth.image.pixelColor(x,y);
      if (after.blue() > before.blue()+3 && after.blue() < 250) ++transfer;
    }
    if (transfer < 1000) return fail("demonstration lacks visible further indirect transfer");
    demo = {{"camera",camera.description()},{"composition",composition.description()},{"furtherBlueTransferPixels",transfer},
        {"oneSurfaceMetrics",first.metrics},{"fourSurfaceMetrics",fourth.metrics},{"scope","same authored scene/camera, depth only; synthetic native A/B, not photographic reference parity"}};
  }
  const QVariantMap receipt{{"passed",true},{"cases",cases},{"maximumChannelError",maximum_error},{"exactTiling",true},{"emissionDemo",demo},{"cancellationObservedBatches",batches->load()},
      {"scope","independent diffuse/mirror cavity energy and mixed BRDF quadrature, captured opacity, haze, persistence, tiling and cancellation; not photographic or dense-interaction qualification"}};
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented);
  if (!proof.isEmpty()) { QFile file(proof+"/receipt.json"); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 1; }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
}
