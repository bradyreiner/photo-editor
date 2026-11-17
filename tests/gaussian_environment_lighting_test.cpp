#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <future>
#include <thread>
#include <iostream>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
namespace {
using Color = std::array<double,3>;
constexpr double pi = 3.14159265358979323846;
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
double encode(double c) { return c <= 0.0031308 ? 12.92*c : 1.055*std::pow(c,1/2.4)-0.055; }
const std::array<QVector3D,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
const std::array<QVector3D,6> rights{{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}}};
const std::array<QVector3D,6> downs{{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}}};
std::shared_ptr<GaussianSceneData> environment(const std::array<Color,6>& colors) {
  auto scene = std::make_shared<GaussianSceneData>(); scene->artifact_sha256 = QString(64,'a');
  // One nearly constant, broad splat per cube face. Orthogonal face centers
  // have zero depth and are culled, so probe radiance is independently known.
  for (size_t i = 0; i < colors.size(); ++i) {
    GaussianPoint p; const auto center = directions[i]*10;
    p.center_opacity = {center.x(),center.y(),center.z(),0.99F};
    p.covariance_a = {1e8F,0,0,1e8F}; p.covariance_b = {0,1e8F,0,0};
    for (size_t c = 0; c < 3; ++c) p.sh[c] = static_cast<float>((colors[i][c]-0.5)/0.2820947918);
    scene->points.push_back(p);
  }
  return scene;
}
GaussianSceneObject sphere() {
  GaussianSceneObject object; object.id = "receiver"; object.name = "Receiver";
  object.position = {}; object.size = {0.5F,0.5F,0.5F}; object.color = {0.5F,0.5F,0.5F}; return object;
}
// Dense polar quadrature, independent of the shader's low-discrepancy samples,
// coordinate hashing and cube UV lookup. This also checks face orientation.
Color irradiance(const std::array<Color,6>& colors, size_t face) {
  Color result{};
  constexpr int latitude = 256, longitude = 512;
  for (int t = 0; t < latitude; ++t) {
    const double theta = (t+0.5)*pi/(2*latitude), cosine = std::cos(theta), sine = std::sin(theta);
    const double weight = cosine*sine*pi/(2*latitude)*2/longitude;
    for (int p = 0; p < longitude; ++p) {
      const double phi = (p+0.5)*2*pi/longitude;
      std::array<double,3> ray{};
      for (size_t a = 0; a < 3; ++a) ray[a] = -directions[face][static_cast<int>(a)]*cosine+
          rights[face][static_cast<int>(a)]*sine*std::cos(phi)+downs[face][static_cast<int>(a)]*sine*std::sin(phi);
      size_t axis = 0; for (size_t a = 1; a < 3; ++a) if (std::abs(ray[a]) > std::abs(ray[axis])) axis = a;
      const auto source = axis*2+(ray[axis] < 0 ? 1U : 0U);
      for (size_t c = 0; c < 3; ++c) result[c] += decode(colors[source][c]*0.99)*weight;
    }
  }
  return result;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  const auto args = app.arguments(); const auto index = args.indexOf("--proof-dir");
  const auto proof = index < 0 ? QString() : args.value(index+1);
  const auto fail = [](const char* reason) { std::cerr << "Environment lighting: " << reason << '\n'; return 1; };
  if (index >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto save = [&](const GaussianFrame& frame,const QString& name) {
    return frame.error.isEmpty() && !frame.image.isNull() && (proof.isEmpty() || frame.image.save(proof+"/"+name+".png"));
  };
  GaussianSceneComposition composition; composition.ambient = 0; composition.environment_diffuse = true;
  composition.light_samples = 1024; composition.objects = {sphere()};
  const auto parsed = gaussian_scene_composition(composition.description());
  if (!parsed || parsed->description() != composition.description() || parsed->description().value("schema") != "looksy-scene-composition-v7") return fail("schema roundtrip");
  for (int fault = 0; fault < 12; ++fault) {
    auto broken = composition.description(); auto env = broken.value("environment").toMap();
    if (fault == 0) env["diffuse"] = 1;
    if (fault == 1) env["strength"] = "1";
    if (fault == 2) env["strength"] = -0.00000001;
    if (fault == 3) env["strength"] = 10.00000001;
    if (fault == 4) env["strength"] = std::numeric_limits<double>::infinity();
    if (fault == 5) env["extra"] = 1;
    if (fault == 6) env.remove("diffuse");
    broken["environment"] = env;
    if (fault == 7) broken["schema"] = "looksy-scene-composition-v6";
    if (fault == 8) broken["environment"] = false;
    if (fault == 9) broken.remove("lightSamples");
    if (fault == 10) broken["lightSamples"] = 8;
    if (fault == 11) broken["schema"] = "future";
    if (gaussian_scene_composition(broken)) return fail("malformed environment accepted");
  }
  QTemporaryDir project;
  const auto persisted = save_gaussian_composition(project.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!persisted.composition || !reopened.composition || reopened.composition->description() != composition.description()) return fail("persistence");
  auto renderer = make_gaussian_scene_renderer();
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.position = {0,0,-3};
  const auto render = [&](const std::shared_ptr<GaussianSceneData>& scene) {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true);
  };
  int max_error = 0;
  const auto check = [&](QColor actual,Color expected,int tolerance) {
    const std::array<int,3> channels{actual.red(),actual.green(),actual.blue()};
    for (size_t c = 0; c < 3; ++c) {
      const auto byte = static_cast<int>(std::lround(std::clamp(encode(expected[c]),0.0,1.0)*255));
      max_error = std::max(max_error,std::abs(channels[c]-byte));
      if (std::abs(channels[c]-byte) > tolerance) { std::cerr << "channel " << c << " actual=" << channels[c] << " expected=" << byte << '\n'; return false; }
    }
    return true;
  };
  std::array<Color,6> uniform; uniform.fill({0.5,0.5,0.5}); auto scene = environment(uniform);
  const double base = decode(0.5), light = decode(0.495);
  for (const float strength : {0.0F,0.5F,1.0F,2.0F}) {
    composition.environment_strength = strength; const auto frame = render(scene);
    const double value = base*light*0.96*strength;
    if (!frame.error.isEmpty() || !check(frame.image.pixelColor(32,32),{value,value,value},1)) return fail("constant-radiance Lambert/strength oracle");
  }
  composition.environment_strength = 1;
  composition.reflections = true; composition.objects[0].color = {1,1,1};
  const auto furnace = render(scene);
  if (!furnace.error.isEmpty() || !check(furnace.image.pixelColor(32,32),{light,light,light},1)) return fail("white furnace double-counted diffuse/reflected energy");
  composition.reflections = false; composition.objects[0].color = {0.5F,0.5F,0.5F};
  composition.objects[0].metallic = 1;
  const auto metal = render(scene);
  if (!metal.error.isEmpty() || !check(metal.image.pixelColor(32,32),{},0)) return fail("metal received a diffuse lobe");
  composition.objects[0].metallic = 0; composition.environment_diffuse = false;
  const auto disabled = render(scene);
  if (!disabled.error.isEmpty() || !check(disabled.image.pixelColor(32,32),{},0)) return fail("disabled illumination changed legacy black");
  composition.environment_diffuse = true;
  uniform.fill({3,3,3}); auto hdr = environment(uniform); composition.objects[0].color = {0.2F,0.2F,0.2F};
  const auto bright = render(hdr); const double expected_hdr = decode(0.2)*decode(2.97)*0.96;
  if (!save(bright,"hdr") || !check(bright.image.pixelColor(32,32),{expected_hdr,expected_hdr,expected_hdr},1)) return fail("probe clipped HDR before material response");
  const std::array<Color,6> colors{{{1,0.2,0.2},{0.2,1,0.2},{0.2,0.2,1},{1,1,0.2},{1,0.2,1},{0.2,1,1}}};
  auto colored = environment(colors); composition.objects[0] = sphere();
  for (size_t face = 0; face < 6; ++face) {
    camera.position = -directions[face]*3; camera.forward = directions[face]; camera.right = rights[face]; camera.down = downs[face];
    const auto frame = render(colored); auto expected = irradiance(colors,face);
    for (auto& c : expected) c *= base*0.96;
    if (!save(frame,"direction-"+QString::number(face)) || !check(frame.image.pixelColor(32,32),expected,3) ||
        frame.metrics.value("environmentProbeReused").toBool() != (face > 0) ||
        frame.metrics.value("scanProbeMs").toDouble() <= 0 ||
        frame.metrics.value("renderMs").toDouble() < frame.metrics.value("scanProbeMs").toDouble()) return fail("directional hemisphere quadrature/probe reuse/timing");
  }
  // At a plane point, a sphere occludes exactly r^2/d^2 of cosine-weighted
  // irradiance. An oblique camera sees that point without hitting the sphere.
  uniform.fill({0.5,0.5,0.5}); scene = environment(uniform);
  composition.capture_lighting = 1; composition.ambient = 1;
  const auto capture_lit = render(scene);
  composition.environment_strength = 0.5F;
  const auto reweighted = render(scene);
  if (!capture_lit.error.isEmpty() || !reweighted.error.isEmpty() || !reweighted.metrics.value("environmentProbeReused").toBool() ||
      capture_lit.image == reweighted.image) return fail("diffuse strength regenerated capture radiance or reused stale shading");
  composition.capture_lighting = 0; composition.ambient = 0; composition.environment_strength = 1;
  GaussianSceneObject floor; floor.id = "floor"; floor.name = "Floor"; floor.kind = 1; floor.position = {}; floor.size = {2,2,0.01F}; floor.color = {0.8F,0.8F,0.8F};
  composition.objects = {floor}; camera.photo.exposure_ev = 2; camera.position = {2,0,-3}; camera.forward = (QVector3D(0,0,-0.01F)-camera.position).normalized();
  camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized(); camera.down = {0,1,0};
  const auto clear = render(scene); if (!save(clear,"unoccluded")) return fail("plane render");
  auto blocker = sphere(); blocker.id = "blocker"; blocker.position = {0,0,-0.5F}; blocker.size = {0.25F,0.25F,0.25F};
  composition.objects.push_back(blocker); const auto blocked = render(scene);
  const double ratio = decode(blocked.image.pixelColor(32,32).redF())/decode(clear.image.pixelColor(32,32).redF());
  const double expected_ratio = 1-0.25*0.25/(0.49*0.49);
  if (!save(blocked,"sphere-contact") || std::abs(ratio-expected_ratio) > 0.015) return fail("analytic sphere contact occlusion");
  const auto mesh = parse_gaussian_obj("v -0.25 -0.25 0\nv 0.25 -0.25 0\nv 0.25 0.25 0\nv -0.25 0.25 0\nf 1 2 3\nf 1 3 4\n");
  if (!mesh.mesh) return fail("mesh fixture");
  composition.objects[1].kind = 2; composition.objects[1].size = {1,1,1}; composition.objects[1].mesh = mesh.mesh; composition.objects[1].mesh_sha256 = mesh.mesh->sha256;
  const auto mesh_shadow = render(scene);
  double blocked_fraction = 0; constexpr int grid = 512; const double step = 0.5/grid, distance_squared = 0.49*0.49;
  for (int y = 0; y < grid; ++y) for (int x = 0; x < grid; ++x) {
    const double dx = -0.25+(x+0.5)*step, dy = -0.25+(y+0.5)*step;
    blocked_fraction += distance_squared*step*step/(pi*std::pow(dx*dx+dy*dy+distance_squared,2));
  }
  const double mesh_ratio = decode(mesh_shadow.image.pixelColor(32,32).redF())/decode(clear.image.pixelColor(32,32).redF());
  if (!save(mesh_shadow,"mesh-contact") || std::abs(mesh_ratio-(1-blocked_fraction)) > 0.015) return fail("independent rectangle view-factor occlusion");
  GaussianRenderOptions tiled; tiled.export_tile_edge = 32;
  if (renderer->renderWithOptions(scene,camera,true,{},tiled).image != mesh_shadow.image ||
      read_gaussian_photo_texture(renderer->render(scene,camera,false)) != mesh_shadow.image) return fail("tile/native texture parity");
  composition.light_samples = 16; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  camera.photo.lens = {3.6F,0.1F,7,15,0.2F,32};
  const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions progressive; progressive.progressive_frames = queue;
  const auto optical = renderer->renderWithOptions(scene,camera,false,{},progressive); const auto partial = queue->takeLatest();
  const auto full_pixels = read_gaussian_photo_texture(optical);
  if (!optical.error.isEmpty() || !partial || partial->metrics.value("apertureSamples").toInt() != 16 || full_pixels.isNull()) return fail("environment pupil refinement");
  auto prefix_camera = camera; prefix_camera.photo.lens.samples = 16;
  if (read_gaussian_photo_texture(*partial) != make_gaussian_scene_renderer()->render(scene,prefix_camera,true).image ||
      renderer->renderWithOptions(scene,camera,true,{},tiled).image != full_pixels) return fail("progressive/tiled environment-pupil pixel parity");
  camera.photo.lens = {}; composition.light_samples = 1024;
  composition.objects[1].visible = false;
  if (render(scene).image != clear.image) return fail("hidden occluder retained a shadow");
  auto moved = composition; moved.objects[0].position.setX(0.1F); camera.composition = std::make_shared<GaussianSceneComposition>(moved);
  const auto shifted = renderer->render(scene,camera,true);
  if (!shifted.error.isEmpty() || shifted.metrics.value("environmentProbeReused").toBool()) return fail("moved anchor reused stale probe");
  // Cancel a live multi-batch integration, rather than only preflight cancellation.
  auto canceled = std::make_shared<std::atomic_bool>(false); auto batches = std::make_shared<std::atomic_int>(0);
  GaussianRenderOptions options; options.canceled = canceled; options.lighting_batches_completed = batches;
  auto cancel_renderer = make_gaussian_scene_renderer();
  auto future = std::async(std::launch::async,[&] { return cancel_renderer->renderWithOptions(scene,camera,true,{},options); });
  QElapsedTimer timer; timer.start();
  while (batches->load() < 2 && future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && timer.elapsed() < 10000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  canceled->store(true); const auto canceled_frame = future.get();
  if (!canceled_frame.canceled || batches->load() < 2 || batches->load() >= 128) return fail("active integration cancellation");
  const QVariantMap receipt{{"passed",true},{"model","object-center-cosine-probe-diffuse-visibility-v1"},
      {"directionalMaximumByteError",max_error},{"sphereOcclusionRatio",ratio},{"sphereExpectedRatio",expected_ratio},
      {"meshOcclusionRatio",mesh_ratio},{"meshExpectedRatio",1-blocked_fraction},{"canceledAfterBatches",batches->load()},
      {"scope","native linear environment diffuse, analytic/numerical visibility oracles, persistence, HDR, texture/tile parity and cancellation; not full GI or photographic qualification"}};
  if (!proof.isEmpty()) { QFile file(proof+"/receipt.json"); if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return fail("receipt"); }
  std::cout << QJsonDocument::fromVariant(receipt).toJson().constData();
  return 0;
}
