#include "gaussian_scene_renderer.h"
#include "gaussian_ply_validation.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <iostream>
#include <thread>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
namespace {
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
double encode(double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; }
GaussianPoint point(float depth,float opacity = 0) {
  GaussianPoint p; p.center_opacity = {0,0,depth,opacity};
  p.covariance_a = {0.01F,0,0,0.01F}; p.covariance_b = {0,0.01F,0,0};
  return p;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv);
  QString proof; const auto args = app.arguments(); const auto i = args.indexOf("--proof-dir");
  if (i >= 0 && i+1 < args.size()) { proof = args[i+1]; QDir().mkpath(proof); }
  const auto fail = [](const char* reason) { std::cerr << reason << '\n'; return 1; };
  GaussianSceneComposition composition; composition.ambient = 0; composition.haze_density = 0.3F;
  composition.haze_distance = 8; composition.haze_color = {0.2F,0.2F,0.2F};
  composition.haze_shadows = true; composition.light_samples = 256;
  GaussianSceneLight sun; sun.id = "sun"; sun.name = "Sun"; sun.vector = {1,0,0}; sun.intensity = 100;
  composition.lights = {sun};
  GaussianSceneObject blocker; blocker.id = "blocker"; blocker.name = "Off-camera blocker"; blocker.kind = 1;
  blocker.position = {3,0,4}; blocker.size = {0.5F,2,2}; blocker.color = {0,0,0};
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point(-10)};
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  auto renderer = make_gaussian_scene_renderer(); QVariantList cases;
  const auto render = [&](const GaussianRenderOptions& options = GaussianRenderOptions{}) {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    return renderer->renderWithOptions(scene,camera,true,{},options);
  };
  const auto check = [&](const char* name,const GaussianFrame& frame,double linear,double tolerance = 1.5) {
    if (!frame.error.isEmpty() || frame.image.isNull()) { std::cerr << name << ": " << frame.error.toStdString() << '\n'; return false; }
    const auto actual = frame.image.pixelColor(camera.image_size.width()/2,camera.image_size.height()/2).red();
    const double expected = 255*encode(linear);
    cases.append(QVariantMap{{"name",name},{"actual",actual},{"expected",expected},{"metrics",frame.metrics}});
    if (!proof.isEmpty()) frame.image.save(proof+"/"+name+".png");
    if (std::abs(actual-expected) > tolerance) { std::cerr << name << " actual=" << actual << " expected=" << expected << '\n'; return false; }
    return true;
  };
  const double tint = decode(0.2), mass = 1-std::exp(-0.3*8), blocked_mass = std::exp(-0.3*2)-std::exp(-0.3*6);
  const auto clear = render();
  if (!check("unoccluded-volume",clear,tint*mass*6)) return fail("unoccluded medium oracle");
  auto second_sun = sun; second_sun.id = "second-sun"; second_sun.name = "Second sun";
  second_sun.vector = {-1,0,0}; second_sun.color = {0.5F,0.5F,0.5F}; second_sun.intensity = 30;
  composition.lights.push_back(second_sun);
  if (!check("multiple-light-volume",render(),tint*mass*(6+0.05*30*decode(0.5)))) return fail("multiple source energy");
  composition.lights = {sun};
  composition.objects = {blocker}; const auto shadow = render();
  if (!check("box-shadow-volume",shadow,tint*(mass+5*(mass-blocked_mass)))) return fail("shadowed medium oracle");
  // Legacy haze must retain its prior unblocked appearance.
  composition.haze_shadows = false;
  if (!check("legacy-volume",render(),tint*mass*6)) return fail("legacy disabled contract");
  composition.haze_shadows = true;
  // A black opaque receiver at z=5 sees only fog in front of it.
  auto receiver = blocker; receiver.id = "receiver"; receiver.name = "Receiver"; receiver.position = {0,0,5.1F}; receiver.size = {1,1,0.1F};
  composition.objects.push_back(receiver);
  const double near_mass = 1-std::exp(-0.3*5), near_blocked = std::exp(-0.3*2)-std::exp(-0.3*5);
  if (!check("solid-depth-volume",render(),tint*(near_mass+5*(near_mass-near_blocked)))) return fail("solid depth integral");
  composition.objects = {blocker};
  // A captured layer gets the integral only up to its own centre depth.
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point(5,0.5F)};
  const double layer = tint*(near_mass+5*(near_mass-near_blocked))+decode(0.5)*std::exp(-0.3*5);
  const double background = tint*(mass+5*(mass-blocked_mass));
  const double expected_layer = decode(0.5*encode(layer)+0.5*encode(background));
  if (!check("captured-depth-volume",render(),expected_layer)) return fail("captured depth integral");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point(-10)};
  // Replacing the box with the same closed OBJ checks transformed mesh BVHs.
  const auto mesh = parse_gaussian_obj(QByteArray("v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 4 8 7 3\nf 1 5 8 4\nf 2 3 7 6\n"));
  if (!mesh.mesh) return fail("mesh fixture");
  auto mesh_blocker = blocker; mesh_blocker.kind = 2; mesh_blocker.mesh = mesh.mesh; mesh_blocker.mesh_sha256 = mesh.mesh->sha256;
  composition.objects = {mesh_blocker};
  if (!check("mesh-shadow-volume",render(),background)) return fail("mesh medium visibility");
  composition.objects = {blocker};
  GaussianRenderOptions tile; tile.export_tile_edge = 32;
  const auto tiled = render(tile);
  if (!tiled.error.isEmpty() || tiled.image != shadow.image) return fail("exact medium tile coordinates");
  GaussianRenderOptions odd_tile; odd_tile.export_tile_edge = 33;
  const auto odd_tiled = render(odd_tile);
  if (!odd_tiled.error.isEmpty() || odd_tiled.image != shadow.image) return fail("odd-origin medium tile coordinates");
  // Integrating a Gaussian shadow along +X has a closed form: multiply
  // exp(-sigma*t) by its perpendicular Gaussian and complete the square.
  composition.objects.clear(); composition.scan_shadows = true;
  scene = std::make_shared<GaussianSceneData>(); auto scan_blocker = point(4,0.8F);
  scan_blocker.center_opacity[0] = 3; scan_blocker.covariance_a = {0.01F,0,0,4}; scan_blocker.covariance_b = {0,4,0,0};
  scene->points = {scan_blocker};
  const auto phi = [](double x) { return 0.5*(1+std::erf(x/std::sqrt(2.0))); };
  const double shadow_integral = 0.3*std::sqrt(2*3.141592653589793*4)*std::exp(-0.3*4+0.3*0.3*4/2)*
      (phi((8-4+0.3*4)/2)-phi((-4+0.3*4)/2));
  if (!check("scan-shadow-volume",render(),tint*(mass+5*(mass-0.8*shadow_integral)))) return fail("scan volume closed-form oracle");
  composition.scan_shadows = false;
  if (!check("scan-shadow-disabled",render(),tint*mass*6)) return fail("scan toggle");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point(-10)};
  // Point illumination: independent high-resolution midpoint quadrature of
  // inverse-square energy, source-segment extinction and view extinction.
  composition.lights[0].kind = 1; composition.lights[0].vector = {3,0,4}; composition.lights[0].intensity = 500;
  double point_integral = 0;
  for (int n = 0; n < 100000; ++n) {
    const double t = (static_cast<double>(n)+0.5)*8/100000;
    const double distance = std::hypot(3.0,t-4);
    point_integral += (8.0/100000)*0.3*std::exp(-0.3*t)*std::exp(-0.3*distance)/(distance*distance);
  }
  if (!check("point-volume",render(),tint*(mass+0.05*500*point_integral))) return fail("point source quadrature");
  // Small one-sided rectangle, independently integrated over its surface.
  composition.lights[0].kind = 2; composition.lights[0].rotation = {0,-90,0};
  composition.lights[0].size = {0.2F,0.2F}; composition.lights[0].intensity = 10000;
  double rectangle_integral = 0;
  for (int n = 0; n < 2000; ++n) for (int u = 0; u < 16; ++u) for (int v = 0; v < 16; ++v) {
    const double t = (static_cast<double>(n)+0.5)*8/2000;
    const double y = ((static_cast<double>(v)+0.5)/16-0.5)*0.2;
    const double z = 4+((static_cast<double>(u)+0.5)/16-0.5)*0.2;
    const double distance = std::sqrt(9+y*y+(z-t)*(z-t));
    rectangle_integral += (8.0/2000/256)*0.3*std::exp(-0.3*t)*std::exp(-0.3*distance)*0.04*3/(distance*distance*distance);
  }
  if (!check("rectangle-volume",render(),tint*(mass+0.05*10000*rectangle_integral))) return fail("rectangle source quadrature");
  composition.lights[0].rotation = {0,90,0};
  if (!check("rectangle-back-volume",render(),tint*mass)) return fail("one-sided rectangle medium");
  // Sphere energy integrated over its subtended solid angle, including the
  // exact entry distance for source-to-volume extinction.
  composition.lights[0].kind = 3; composition.lights[0].radius = 0.1F; composition.lights[0].intensity = 10000;
  double sphere_integral = 0;
  for (int n = 0; n < 2000; ++n) for (int m = 0; m < 256; ++m) {
    const double t = (static_cast<double>(n)+0.5)*8/2000, distance = std::hypot(3.0,t-4);
    const double cap = 1-std::sqrt(1-0.01/(distance*distance));
    const double cosine = 1-cap*(static_cast<double>(m)+0.5)/256;
    const double segment = distance*cosine-std::sqrt(std::max(0.0,0.01-distance*distance*(1-cosine*cosine)));
    sphere_integral += (8.0/2000/256)*0.3*std::exp(-0.3*t)*std::exp(-0.3*segment)*2*3.141592653589793*cap;
  }
  if (!check("sphere-volume",render(),tint*(mass+0.05*10000*sphere_integral))) return fail("sphere source quadrature");
  composition.lights = {sun}; composition.lights[0].angular_radius = 0.03F;
  if (!check("finite-sun-volume",render(),tint*mass*6)) return fail("finite sun medium");
  composition.lights[0].angular_radius = 0; composition.lights[0].intensity = 10;
  composition.haze_anisotropy = 0.4F; composition.lights[0].vector = {0,0,1};
  if (!check("forward-phase-volume",render(),tint*mass*(1+0.5*1.4/(0.6*0.6)))) return fail("forward phase oracle");
  composition.lights[0].vector = {0,0,-1};
  if (!check("backward-phase-volume",render(),tint*mass*(1+0.5*0.6/(1.4*1.4)))) return fail("backward phase oracle");
  composition.haze_anisotropy = 0;
  composition.lights = {sun}; composition.objects = {blocker};
  camera.photo.lens = {4,0.1F,0,0,0,16};
  const auto optical = render(), optical_tiles = render(tile);
  int max_optical_error = 0;
  if (!optical.error.isEmpty() || !optical_tiles.error.isEmpty()) return fail("optical volume render");
  for (int y = 0; y < optical.image.height(); ++y) for (int x = 0; x < optical.image.width(); ++x)
    max_optical_error = std::max(max_optical_error,std::abs(optical.image.pixelColor(x,y).red()-optical_tiles.image.pixelColor(x,y).red()));
  if (max_optical_error > 1) return fail("optical volume tile coordinates");
  camera.photo = {};
  // A front-facing mirror sees the medium on its reflected segment; the
  // camera-to-mirror segment contributes separately. Neither can reuse the
  // primary ray's unoccluded fog color.
  auto mirror = blocker; mirror.id = "mirror"; mirror.name = "Mirror"; mirror.position = {}; mirror.size = {100,100,0.01F};
  mirror.color = {1,1,1}; mirror.metallic = 1; mirror.roughness = 0.04F;
  auto reflected_blocker = blocker; reflected_blocker.position = {3,0,-4};
  composition.objects = {mirror,reflected_blocker}; composition.reflections = true; composition.spatial_reflections = true;
  camera.image_size = {1,1}; camera.fx = camera.fy = 1; camera.cx = camera.cy = 0.5F; camera.position = {0,0,-3};
  const double primary_mass = 1-std::exp(-0.3*2.99), primary_blocked = 1-std::exp(-0.3);
  const double reflected_blocked = std::exp(-0.3*1.98999)-std::exp(-0.3*5.98999);
  const double reflected_radiance = tint*(primary_mass+5*(primary_mass-primary_blocked))+
      std::exp(-0.3*2.99)*tint*(mass+5*(mass-reflected_blocked));
  if (!check("spatial-reflected-volume",render(),reflected_radiance,2)) return fail("secondary volume integral");
  composition.surface_bounces = 2;
  if (!check("path-reflected-volume",render(),reflected_radiance,2)) return fail("multiple-surface segment volume");
  composition.reflections = false; composition.spatial_reflections = false; composition.surface_bounces = 1;
  composition.objects = {blocker}; camera.position = {}; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  const auto codec = gaussian_scene_composition(composition.description());
  if (!codec || !codec->haze_shadows || codec->description() != composition.description()) return fail("v16 codec");
  for (const auto& value : {QVariant(1),QVariant("true"),QVariant()}) {
    auto bad = composition.description(); bad["hazeShadows"] = value; if (gaussian_scene_composition(bad)) return fail("invalid haze shadow flag");
  }
  QTemporaryDir project;
  if (!save_gaussian_composition(project.path(),"artifact",composition,{}).composition) return fail("save medium");
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!reopened.composition || reopened.composition->description() != composition.description()) return fail("reopen medium");
  // Native previews can use a different angular grid from an equal-sized
  // export. They must never satisfy each other's completed-source cache key.
  const auto cache_camera = camera;
  camera.image_size = {129,129}; camera.fx = camera.fy = 1500; camera.cx = camera.cy = 64.5F;
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto native_domain = renderer->render(scene,camera,false);
  const auto export_domain = render();
  if (!native_domain.error.isEmpty() || !native_domain.native_texture || native_domain.metrics.value("hazeFieldCellPixels").toInt() <= 4 ||
      !export_domain.error.isEmpty() || export_domain.metrics.value("hazeFieldCellPixels").toInt() != 4 || export_domain.metrics.value("resolvedSourceReused").toBool()) return fail("native/export haze cache separation");
  const auto repeat_export = render();
  if (!repeat_export.metrics.value("resolvedSourceReused").toBool() || repeat_export.image != export_domain.image) return fail("same-domain haze cache reuse");
  camera = cache_camera;
  // Cancellation after an observed completed haze batch cannot publish pixels.
  GaussianRenderOptions cancel; cancel.canceled = std::make_shared<std::atomic_bool>(false);
  cancel.lighting_batches_completed = std::make_shared<std::atomic_int>(0);
  camera.image_size = {257,257}; camera.fx = camera.fy = 240; camera.cx = camera.cy = 128.5F;
  std::jthread stop([&] { const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10); while (cancel.lighting_batches_completed->load() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield(); cancel.canceled->store(true); });
  const auto interrupted = render(cancel); stop.join();
  if (!interrupted.canceled || !interrupted.image.isNull() || interrupted.native_texture) return fail("medium batch cancellation");
  QVariantList field_bounds;
  if (args.contains("--field-bounds")) {
    composition = {}; composition.haze_shadows = true; composition.haze_density = 0.1F; composition.light_samples = 1024;
    camera = {}; camera.image_size = {8192,8192}; camera.fx = camera.fy = 6000; camera.cx = camera.cy = 4096;
    for (const bool optical_bounds : {false,true}) {
      camera.photo = {};
      if (optical_bounds) {
        camera.photo.lens = {4,0.05F,0,0,0,16}; camera.photo.lens.spherical_distortion = 0.1F;
        camera.photo.lens.astigmatism = 0.05F; camera.photo.lens.field_curvature = 0.05F;
      }
      GaussianRenderOptions bounded; bounded.canceled = std::make_shared<std::atomic_bool>(false);
      bounded.lighting_batches_completed = std::make_shared<std::atomic_int>(0);
      std::jthread stopper([&] { const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(10); while (bounded.lighting_batches_completed->load() == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield(); bounded.canceled->store(true); });
      const auto canceled_field = render(bounded); stopper.join();
      if (!canceled_field.error.isEmpty() || !canceled_field.canceled || bounded.lighting_batches_completed->load() == 0 || !canceled_field.image.isNull() || canceled_field.native_texture) return fail("8K maximum-quality haze field admission/cancellation");
      field_bounds.append(QVariantMap{{"optical",optical_bounds},{"completedBatches",bounded.lighting_batches_completed->load()},{"canceled",true}});
    }
  }
  QVariantMap dense;
  const auto dense_arg = args.indexOf("--dense-project");
  if (dense_arg >= 0 && dense_arg+1 < args.size()) {
    const auto project_path = args[dense_arg+1];
    const auto ply = QDir(project_path).filePath("trained/scene.ply");
    const auto validation = validate_gaussian_ply(ply,{});
    const auto loaded = load_gaussian_scene(ply,QDir(project_path).filePath("dataset/sparse"),validation.value("artifactSha256").toString());
    if (!loaded.scene || loaded.scene->cameras.empty()) { std::cerr << loaded.error.toStdString() << '\n'; return fail("Dense haze input unavailable"); }
    camera = loaded.scene->cameras.front();
    const auto width_arg = args.indexOf("--dense-width");
    bool valid_width = true; const auto dense_width = width_arg >= 0 ? args.value(width_arg+1).toInt(&valid_width) : 640;
    if (!valid_width || dense_width < 32 || dense_width > 4096) return fail("Dense width must be 32-4096");
    const float scale = static_cast<float>(dense_width)/static_cast<float>(camera.image_size.width());
    camera.image_size = {dense_width,qRound(static_cast<float>(camera.image_size.height())*scale)};
    camera.fx *= scale; camera.fy *= scale; camera.cx *= scale; camera.cy *= scale;
    const float focus = loaded.scene->focus_distance;
    composition = {}; composition.haze_density = 0.15F/focus; composition.haze_distance = focus*3;
    composition.haze_color = {0.25F,0.3F,0.4F}; composition.light_samples = 16;
    sun.vector = camera.right; sun.intensity = 60; composition.lights = {sun};
    blocker.position = camera.position+camera.forward*(focus*0.9F)+camera.right*(focus*0.5F);
    blocker.size = {focus*0.15F,focus*0.3F,focus*0.3F}; composition.objects = {blocker};
    const auto render_dense = [&] {
      camera.composition = std::make_shared<GaussianSceneComposition>(composition);
      auto result = renderer->render(loaded.scene,camera,!args.contains("--dense-native"));
      if (result.native_texture && result.image.isNull()) result.image = read_gaussian_photo_texture(result);
      return result;
    };
    const auto legacy = render_dense(); composition.haze_shadows = true; composition.scan_shadows = true;
    const auto shadowed = render_dense();
    if (!legacy.error.isEmpty() || !shadowed.error.isEmpty() || legacy.image.isNull() || shadowed.image.isNull() || legacy.image == shadowed.image) return fail("Dense shadowed haze output");
    if (!proof.isEmpty() && (!legacy.image.save(proof+"/dense-legacy.png") || !shadowed.image.save(proof+"/dense-shadowed.png"))) return fail("Dense haze capture");
    dense = {{"project",project_path},{"pointCount",static_cast<qulonglong>(loaded.scene->points.size())},{"legacyMetrics",legacy.metrics},{"shadowedMetrics",shadowed.metrics},
        {"scope","Dense controlled render with scan shadows, not photographic or foreground interaction qualification"}};
  }
  if (!proof.isEmpty()) {
    QFile file(proof+"/receipt.json"); if (!file.open(QIODevice::WriteOnly)) return fail("receipt");
    file.write(QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"cases",cases},{"tileExact",true},{"oddOriginTilesExact",true},{"opticalTileMaxError",max_optical_error},{"persistence",true},{"observedBatchCancellation",true},{"nativeExportCacheSeparated",true},{"dense",dense},{"fieldBounds",field_bounds}}).toJson());
  }
  std::cout << "Shadowed haze: independent extinction-mass, blocker interval, solid/captured depths, mesh visibility, tiling, persistence and cancellation pass.\n";
  return 0;
}
