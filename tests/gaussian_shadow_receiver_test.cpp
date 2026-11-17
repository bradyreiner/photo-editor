#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
double decode(double x) { return x <= 0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4); }
double encode(double x) { return x <= 0.0031308 ? 12.92*x : 1.055*std::pow(x,1/2.4)-0.055; }
GaussianPoint particle() {
  GaussianPoint p; p.center_opacity = {0,0,3,1};
  p.covariance_a = {0.09F,0,0,0.09F}; p.covariance_b = {0,0.000001F,0,0};
  const std::array<float,3> color{0.6F,0.35F,0.8F};
  for (size_t c = 0; c < 3; ++c) p.sh[c] = (color[c]-0.5F)/0.2820947918F;
  return p;
}
// Independent double-precision segment/slab oracle for the axis-aligned box.
bool blocked(const GaussianSceneObject& box,double x,double y,double z) {
  double low = 0, high = 1;
  const std::array<double,3> origin{0,0,3-1e-5}, delta{x,y,z-(3-1e-5)};
  for (int axis = 0; axis < 3; ++axis) {
    const auto a = static_cast<size_t>(axis);
    const double lower = box.position[axis]-box.size[axis], upper = box.position[axis]+box.size[axis];
    if (std::abs(delta[a]) < 1e-15) { if (origin[a] < lower || origin[a] > upper) return false; continue; }
    const double first = (lower-origin[a])/delta[a], last = (upper-origin[a])/delta[a];
    low = std::max(low,std::min(first,last)); high = std::min(high,std::max(first,last));
  }
  return low <= high && high > 0 && low < 1;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); QTemporaryDir project;
  const auto args = app.arguments(); const auto proof_index = args.indexOf("--proof-dir");
  const QString proof = proof_index < 0 ? QString() : args.value(proof_index+1);
  if (!proof.isEmpty() && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto fail = [](const char* reason) { std::cerr << "Shadow receiver: " << reason << '\n'; return 1; };
  GaussianSceneComposition composition; composition.capture_lighting = 2;
  const auto description = composition.description(); const auto parsed = gaussian_scene_composition(description);
  if (!parsed || parsed->description() != description || description.value("schema") != "looksy-scene-composition-v17") return fail("v17 roundtrip");
  auto invalid = description; invalid["schema"] = "looksy-scene-composition-v16";
  if (gaussian_scene_composition(invalid)) return fail("old schema accepted new mode");
  invalid = description; invalid.remove("hazeShadows");
  if (gaussian_scene_composition(invalid)) return fail("incomplete v17 accepted");
  invalid = description; auto lighting = invalid.value("captureLighting").toMap(); lighting["mode"] = "invalid"; invalid["captureLighting"] = lighting;
  if (gaussian_scene_composition(invalid)) return fail("unknown lighting mode accepted");
  const auto saved = save_gaussian_composition(project.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!saved.composition || !reopened.composition || reopened.composition->description() != description) return fail("persistence");
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {particle()};
  GaussianCamera camera; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  auto renderer = make_gaussian_scene_renderer();
  const auto neutral = renderer->render(scene,camera,true);
  if (!neutral.error.isEmpty() || neutral.image.isNull()) return fail("neutral render");
  QVariantList cases; double maximum_error = 0;
  const auto render = [&](GaussianRenderOptions options = {}) {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    return renderer->renderWithOptions(scene,camera,true,{},options);
  };
  const auto check = [&](const char* name,const std::array<double,3>& ratio,bool exact = false) {
    const auto frame = render();
    if (!frame.error.isEmpty() || frame.image.isNull()) { std::cerr << frame.error.toStdString() << '\n'; return false; }
    const QColor actual = frame.image.pixelColor(64,64), original = neutral.image.pixelColor(64,64);
    const std::array<int,3> pixel{actual.red(),actual.green(),actual.blue()}, base{original.red(),original.green(),original.blue()};
    double error = 0;
    for (size_t c = 0; c < 3; ++c) error = std::max(error,std::abs(pixel[c]-255*encode(decode(base[c]/255.0)*ratio[c])));
    maximum_error = std::max(maximum_error,error);
    const bool passed = error < 2 && (!exact || frame.image == neutral.image);
    cases.append(QVariantMap{{"case",name},{"passed",passed},{"maximumCenterCodeError",error},{"exactNeutralPixels",frame.image == neutral.image},{"metrics",frame.metrics}});
    if (!proof.isEmpty() && !frame.image.save(proof+"/"+name+".png")) return false;
    if (!passed) std::cerr << name << " error=" << error << " exact=" << (frame.image == neutral.image) << '\n';
    return passed;
  };
  composition.ambient = 0;
  if (!check("no-light",{1,1,1},true)) return fail("zero reference identity");
  GaussianSceneLight key; key.id = "key"; key.name = "Key"; key.vector = {-1,0,-1}; key.intensity = 2;
  key.color = {1,0.2F,0.5F}; composition.lights = {key};
  if (!check("unoccluded-colored",{1,1,1},true)) return fail("unoccluded identity");
  GaussianSceneObject box; box.id = "blocker"; box.name = "Blocker"; box.kind = 1;
  box.position = {-0.65F,0,2.35F}; box.size = {0.18F,0.25F,0.18F}; composition.objects = {box};
  if (!check("fully-blocked",{0,0,0})) return fail("full shadow");
  composition.shadow_strength = 0.4F;
  if (!check("partial-strength",{0.6,0.6,0.6})) return fail("shadow strength");
  composition.shadow_strength = 0;
  if (!check("zero-strength",{1,1,1})) return fail("zero shadow strength");
  composition.shadow_strength = 1; composition.ambient = 0.12F;
  auto fill = key; fill.id = "fill"; fill.name = "Fill"; fill.vector = {1,0,-1}; fill.color = {0.1F,1,0.3F}; fill.intensity = 0.8F;
  composition.lights.push_back(fill);
  std::array<double,3> ratio{};
  for (int c = 0; c < 3; ++c) {
    const double a = 2*std::sqrt(0.5)/M_PI*decode(key.color[c]);
    const double b = 0.8*std::sqrt(0.5)/M_PI*decode(fill.color[c]);
    ratio[static_cast<size_t>(c)] = (0.12+b)/(0.12+a+b);
  }
  if (!check("colored-fill-and-ambient",ratio)) return fail("channel-wise differential oracle");
  // Rotate the entire experiment so the same oracle also exercises normals
  // derived from correlated covariance and transformed receiver/blocker rays.
  const auto original_camera = camera; const auto original_composition = composition;
  const auto rotation = QQuaternion::fromEulerAngles(24,37,11);
  auto rotated_scene = std::make_shared<GaussianSceneData>(*scene); auto& point = rotated_scene->points[0];
  const auto center = rotation.rotatedVector({0,0,3}), normal = rotation.rotatedVector({0,0,-1});
  point.center_opacity = {center.x(),center.y(),center.z(),1};
  const auto covariance = [&](int a,int b) { return (a == b ? 0.09F : 0)+(0.000001F-0.09F)*normal[a]*normal[b]; };
  point.covariance_a = {covariance(0,0),covariance(0,1),covariance(0,2),covariance(1,1)};
  point.covariance_b = {covariance(1,2),covariance(2,2),0,0};
  scene = rotated_scene; camera.right = rotation.rotatedVector(camera.right);
  camera.down = rotation.rotatedVector(camera.down); camera.forward = rotation.rotatedVector(camera.forward);
  for (auto& light : composition.lights) light.vector = rotation.rotatedVector(light.vector);
  composition.objects[0].position = rotation.rotatedVector(composition.objects[0].position);
  composition.objects[0].rotation = rotation.toEulerAngles();
  if (!check("rotated-receiver",ratio)) return fail("correlated normal differential oracle");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {particle()};
  camera = original_camera; composition = original_composition;
  composition.capture_emission = true; composition.objects[0].emission = 10;
  if (!check("emission-preference-inactive",ratio)) return fail("receiver added emission");
  composition.capture_emission = false; composition.objects[0].emission = 0;
  composition.capture_power = 0;
  if (!check("zero-power",{0,0,0})) return fail("capture brightness");
  composition.capture_power = 1; composition.ambient = 0.04F;
  auto area = key; area.kind = 2; area.vector = {-1,0,1}; area.size = {2,1}; area.color = {1,1,1}; area.intensity = 3;
  composition.lights = {area}; composition.light_samples = 1024;
  // Numerical area integral uses a regular 512x512 grid, independent of the
  // renderer's 1024 quasi-random samples and eight-sample GPU batches.
  double reference = 0, received = 0;
  constexpr int steps = 512;
  for (int y = 0; y < steps; ++y) for (int x = 0; x < steps; ++x) {
    const double px = -1+2*((x+0.5)/steps-0.5), py = (y+0.5)/steps-0.5;
    const double squared = px*px+py*py+4;
    const double weight = 3*8/(M_PI*squared*squared);
    reference += weight; if (!blocked(box,px,py,1)) received += weight;
  }
  const double area_ratio = (0.04+received/(steps*steps))/(0.04+reference/(steps*steps));
  if (area_ratio < 0.1 || area_ratio > 0.9 || !check("area-penumbra",{area_ratio,area_ratio,area_ratio})) return fail("finite-source ratio of integrals");
  const auto full = render(); GaussianRenderOptions options; options.export_tile_edge = 32;
  const auto tiled = render(options);
  const auto difference = [](const QImage& a,const QImage& b) {
    if (a.isNull() || a.size() != b.size()) return 256;
    int maximum = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
      const auto p = a.pixelColor(x,y), q = b.pixelColor(x,y);
      maximum = std::max({maximum,std::abs(p.red()-q.red()),std::abs(p.green()-q.green()),std::abs(p.blue()-q.blue())});
    }
    return maximum;
  };
  composition.capture_lighting = 0;
  const auto captured_full = render(), captured_tiled = render(options);
  const int tile_error = difference(full.image,tiled.image), baseline_tile_error = difference(captured_full.image,captured_tiled.image);
  std::cout << "Tile maximum RGB code error: receiver=" << tile_error << " captured=" << baseline_tile_error << '\n';
  if (!full.error.isEmpty() || !tiled.error.isEmpty() || !captured_full.error.isEmpty() || !captured_tiled.error.isEmpty() ||
      tile_error > 1 || baseline_tile_error > 1) return fail("tiled shadow output exceeded one-code raster roundoff");
  composition.capture_lighting = 2;
  composition.objects.clear();
  if (!check("unoccluded-area",{1,1,1},true)) return fail("batched area identity");
  composition.lights[0].visible = false;
  if (!check("hidden-light",{1,1,1},true)) return fail("hidden light identity");
  if (!proof.isEmpty()) {
    const QVariantMap receipt{{"passed",true},{"cases",cases},{"maximumCenterCodeError",maximum_error},{"areaOracleRatio",area_ratio},
        {"maximumTiledCodeError",tile_error},{"capturedBaselineTiledCodeError",baseline_tile_error},
        {"scope","Synthetic differential-shadow math, identity, persistence and tiling; finished-scene visual qualification remains separate"}};
    QFile file(proof+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes) != bytes.size()) return 2;
  }
  std::cout << "Shadow receiver: " << cases.size() << " cases, area-integral oracle and bounded tiling passed\n";
  return 0;
}
