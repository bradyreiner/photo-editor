#include "gaussian_scene_renderer.h"
#include "gaussian_focus_pick.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
namespace {
bool require(bool passed, const char* reason) { if (!passed) std::cerr << reason << '\n'; return passed; }
GaussianPoint splat(float z, std::array<float,3> color) {
  GaussianPoint point; point.center_opacity = {0,0,z,0.5F};
  point.covariance_a = {0.04F,0,0,0.04F}; point.covariance_b = {0,0.04F,0,0};
  for (size_t i = 0; i < 3; ++i) point.sh[i] = (color[i]-0.5F)/0.2820947918F;
  return point;
}
GaussianSceneObject sphere() {
  GaussianSceneObject result; result.id = "sphere"; result.name = "Sphere"; result.position = {0,0,3}; result.size = {0.5F,0.5F,0.5F}; result.color = {0,1,0}; return result;
}
double linear(int value) { const double c = double(value)/255; return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
bool near_images(const QImage& a, const QImage& b, int tolerance = 1) {
  if (a.size() != b.size() || a.isNull()) return false;
  for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
    const auto p = a.pixelColor(x,y), q = b.pixelColor(x,y);
    if (std::abs(p.red()-q.red()) > tolerance || std::abs(p.green()-q.green()) > tolerance || std::abs(p.blue()-q.blue()) > tolerance) return false;
  }
  return true;
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc,argv);
  GaussianSceneComposition composition; composition.ambient = 1; composition.objects = {sphere()};
  const auto parsed = gaussian_scene_composition(composition.description());
  if (!require(parsed && parsed->description() == composition.description(), "Composition roundtrip failed")) return 1;
  auto bad = composition; bad.objects[0].size.setX(0);
  if (!require(!gaussian_scene_composition(bad.description()), "Zero extent accepted")) return 1;
  bad = composition; bad.objects.push_back(bad.objects[0]);
  if (!require(!gaussian_scene_composition(bad.description()), "Duplicate object id accepted")) return 1;
  bad = composition; bad.objects[0].roughness = std::numeric_limits<float>::infinity();
  if (!require(!gaussian_scene_composition(bad.description()), "Nonfinite material accepted")) return 1;
  QTemporaryDir temporary;
  const auto initial = load_gaussian_composition(temporary.path(), "artifact");
  const auto saved = save_gaussian_composition(temporary.path(), "artifact", composition, {});
  const auto reopened = load_gaussian_composition(temporary.path(), "artifact");
  if (!require(initial.composition && saved.composition && reopened.composition && reopened.composition->description() == composition.description(), "Object persistence failed") ||
      !require(!save_gaussian_composition(temporary.path(), "artifact", composition, {}).error.isEmpty(), "Stale writer overwrote scene") ||
      !require(!load_gaussian_composition(temporary.path(), "other-artifact").error.isEmpty(), "Wrong artifact accepted scene objects")) return 1;
  auto renderer = make_gaussian_scene_renderer();
  GaussianCamera camera; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {splat(4,{1,0,0})};
  const auto baseline = renderer->render(scene,camera,true);
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto solid = renderer->render(scene,camera,true);
  if (!require(solid.error.isEmpty() && solid.image.pixelColor(64,64) == QColor(0,255,0), "Solid did not occlude the rear splat")) { std::cerr << solid.error.toStdString(); return 1; }
  auto front = std::make_shared<GaussianSceneData>(); front->points = {splat(2,{1,0,0})};
  const auto layered = renderer->render(front,camera,true);
  const auto pixel = layered.image.pixelColor(64,64);
  if (!require(layered.error.isEmpty() && std::abs(pixel.red()-128) <= 1 && std::abs(pixel.green()-128) <= 1 && pixel.blue() == 0, "Front splat transmittance over solid is incorrect")) return 1;
  auto hidden = composition; hidden.objects[0].visible = false; camera.composition = std::make_shared<GaussianSceneComposition>(hidden);
  if (!require(renderer->render(scene,camera,true).image == baseline.image, "Hidden objects changed the splat-only image")) return 1;
  scene = std::make_shared<GaussianSceneData>(); scene->points = {splat(-4,{1,0,0})};
  composition.objects[0].color = {1,1,1}; composition.ambient = 0;
  GaussianSceneLight lamp; lamp.id = "light"; lamp.name = "Light"; lamp.kind = 1; lamp.vector = {0,0,0}; lamp.intensity = 5;
  composition.lights = {lamp}; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto near = renderer->render(scene,camera,true);
  composition.lights[0].vector.setZ(-2.5F); camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto far = renderer->render(scene,camera,true);
  const double ratio = linear(near.image.pixelColor(64,64).red())/linear(far.image.pixelColor(64,64).red());
  if (!require(near.error.isEmpty() && far.error.isEmpty() && std::abs(ratio-4) < 0.12, "Point-light inverse-square attenuation failed")) { std::cerr << "ratio=" << ratio; return 1; }
  GaussianSceneObject floor; floor.id = "floor"; floor.name = "Floor"; floor.kind = 1; floor.position = {0,0,4}; floor.size = {1.5F,1.5F,0.1F}; floor.color = {1,1,1};
  lamp.kind = 0; lamp.vector = {-1,0,-1}; lamp.intensity = 2;
  composition.objects = {floor}; composition.lights = {lamp}; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto lit = renderer->render(scene,camera,true);
  auto blocker = sphere(); blocker.position = {-0.5F,0,3.4F}; blocker.size = {0.2F,0.2F,0.2F}; composition.objects.push_back(blocker);
  camera.composition = std::make_shared<GaussianSceneComposition>(composition); const auto shadow = renderer->render(scene,camera,true);
  if (!require(lit.error.isEmpty() && shadow.error.isEmpty() && lit.image.pixelColor(64,64).red() > 100 && shadow.image.pixelColor(64,64).red() == 0, "Added objects did not cast the expected ray-traced shadow")) return 1;
  composition.ambient = 1; composition.lights.clear(); composition.objects = {floor};
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto pinhole = renderer->render(scene,camera,true);
  camera.photo.lens = {3.9F,0.15F,0,0,0,32};
  const auto focused = renderer->render(scene,camera,true);
  if (!require(focused.error.isEmpty() && near_images(focused.image,pinhole.image), "Focused solid plane changed under aperture integration")) return 1;
  camera.photo = {}; camera.image_size = {257,193}; camera.fx = camera.fy = 200; camera.cx = 128.5F; camera.cy = 96.5F;
  const auto full = renderer->render(scene,camera,true);
  GaussianRenderOptions tiled_options; tiled_options.export_tile_edge = 64;
  const auto tiled = renderer->renderWithOptions(scene,camera,true,{},tiled_options);
  if (!require(tiled.error.isEmpty() && near_images(full.image,tiled.image,0), "Object tile rays or splat occlusion changed at tile boundaries")) return 1;
  camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  composition.objects = {sphere()}; composition.ambient = 1; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto focus = pick_gaussian_focus(*scene,camera,{64.5,64.5},{},{});
  if (!require(focus.value("ok").toBool() && std::abs(focus.value("focusDistance").toDouble()-2.5) < 1e-5, "Focus picking ignored added object surface")) return 1;
  const auto first = renderer->render(scene,camera,true);
  composition.objects[0].color = {0,0,1}; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto changed = renderer->render(scene,camera,true);
  if (!require(changed.error.isEmpty() && changed.image != first.image && !changed.metrics.value("resolvedSourceReused").toBool(), "Object edit reused stale resolved pixels")) return 1;
  camera.photo.exposure_ev = -1; const auto exposure = renderer->render(scene,camera,true);
  if (!require(exposure.error.isEmpty() && exposure.metrics.value("resolvedSourceReused").toBool(), "Film-only edit did not reuse completed object/splat image")) return 1;
  // Six independent colored directions detect probe-face and reflection-sign
  // mistakes. A centered mirror reflects the opposite side of the camera view.
  const std::array<QVector3D,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
  const std::array<QVector3D,6> rights{{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}}};
  const std::array<QVector3D,6> downs{{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}}};
  const std::array<std::array<float,3>,6> colors{{{1,0,0},{0,1,0},{0,0,1},{1,1,0},{1,0,1},{0,1,1}}};
  auto environment = std::make_shared<GaussianSceneData>();
  for (size_t face = 0; face < 6; ++face) {
    auto point = splat(0,colors[face]); const auto position = directions[face]*5;
    point.center_opacity[0] = position.x(); point.center_opacity[1] = position.y(); point.center_opacity[2] = position.z(); environment->points.push_back(point);
  }
  composition.objects = {sphere()}; composition.objects[0].position = {0,0,0}; composition.objects[0].color = {1,1,1};
  composition.objects[0].metallic = 1; composition.objects[0].roughness = 0.04F; composition.ambient = 0; composition.reflections = true;
  camera.photo = {}; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  for (size_t face = 0; face < 6; ++face) {
    camera.position = -directions[face]*3; camera.forward = directions[face]; camera.right = rights[face]; camera.down = downs[face];
    const auto reflected = renderer->render(environment,camera,true);
    if (!require(reflected.error.isEmpty(), "Scan reflection probe failed")) { std::cerr << reflected.error.toStdString(); return 1; }
    const auto color = reflected.image.pixelColor(64,64); const std::array<int,3> actual{color.red(),color.green(),color.blue()};
    for (size_t channel = 0; channel < 3; ++channel)
      if (!require(colors[face^1U][channel] == 0 ? actual[channel] <= 2 : actual[channel] >= 100 && actual[channel] <= 150, "Mirror reflection sampled the wrong captured direction")) return 1;
    if (!require(reflected.metrics.value("reflectionProbeReused").toBool() == (face > 0), "Reflection probe did not invalidate/reuse at the expected scope")) return 1;
  }
  const auto reflected_full = renderer->render(environment,camera,true);
  const auto reflected_tiles = renderer->renderWithOptions(environment,camera,true,{},tiled_options);
  if (!require(reflected_tiles.error.isEmpty() && near_images(reflected_full.image,reflected_tiles.image,0), "Reflection probe changed across tile boundaries")) return 1;
  composition.objects[0].position.setX(0.1F); camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  if (!require(!renderer->render(environment,camera,true).metrics.value("reflectionProbeReused").toBool(), "Moving the reflection anchor reused a stale probe")) return 1;
  // An off-axis mirror ray checks face orientation, not only face identity.
  camera.position = {0,0,-3}; camera.right = {1,0,0}; camera.down = {0,1,0}; camera.forward = {0,0,1};
  composition.objects[0].position = {0,0,0}; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto ray = QVector3D((69.5F-camera.cx)/camera.fx,(66.5F-camera.cy)/camera.fy,1).normalized();
  const double b = QVector3D::dotProduct(camera.position,ray), c = camera.position.lengthSquared()-0.25;
  const float distance = static_cast<float>(-b-std::sqrt(b*b-c));
  const auto normal = (camera.position+ray*distance).normalized();
  const auto reflected_ray = ray-normal*(2*QVector3D::dotProduct(ray,normal));
  auto asymmetric = std::make_shared<GaussianSceneData>(); auto marker = splat(0,{1,0,0});
  const auto marker_position = reflected_ray*5;
  marker.center_opacity[0] = marker_position.x(); marker.center_opacity[1] = marker_position.y(); marker.center_opacity[2] = marker_position.z(); asymmetric->points = {marker};
  const auto oriented = renderer->render(asymmetric,camera,true);
  if (!require(oriented.error.isEmpty() && oriented.image.pixelColor(69,66).red() > 80 && oriented.image.pixelColor(69,66).green() < 2,
      "Off-axis scan reflection has a mirrored or rotated probe face")) return 1;
  auto bright = std::make_shared<GaussianSceneData>(); bright->points = {splat(-5,{3,3,3})};
  composition.objects[0].metallic = 0; camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto highlight = renderer->render(bright,camera,true);
  if (!require(highlight.error.isEmpty() && highlight.image.pixelColor(64,64).red() > 75 && highlight.image.pixelColor(64,64).red() < 100,
      "Scan reflections clipped reconstructed over-white light before material reflection")) return 1;
  // Independent Lambert and Beer-Lambert oracles: no source renderer is used
  // to produce expected pixels. Half-opacity also checks trained compositing.
  camera = GaussianCamera{}; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  auto captured = std::make_shared<GaussianSceneData>(); captured->points = {splat(4,{0.6F,0.2F,0.1F})};
  captured->points[0].covariance_b[1] = 0.0001F;
  GaussianSceneComposition effects; effects.ambient = 0; effects.capture_lighting = 1;
  lamp.kind = 0; lamp.vector = {0,0,-1}; lamp.intensity = float(M_PI); effects.lights = {lamp};
  const auto encoded = [](double x) { return x <= 0.0031308 ? 12.92*x : 1.055*std::pow(x,1/2.4)-0.055; };
  const auto decoded = [](double x) { return x <= 0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4); };
  const auto pixels = [&](const GaussianSceneComposition& settings, const std::shared_ptr<GaussianSceneData>& data) {
    camera.composition = std::make_shared<GaussianSceneComposition>(settings); return renderer->render(data,camera,true);
  };
  const auto lambert = pixels(effects,captured);
  if (!require(lambert.error.isEmpty() && std::abs(lambert.image.pixelColor(64,64).red()-77) <= 1 &&
      lambert.metrics.value("captureLightingBytes").toInt() == 32, "Planar captured-color Lambert lighting failed")) { std::cerr << lambert.error.toStdString(); return 1; }
  auto temporal_capture = std::make_shared<GaussianSceneData>(*captured);
  temporal_capture->points[0].covariance_b[1] = 0.04F;
  GaussianTemporalPoint conditional; conditional.normalization[0] = 1;
  conditional.covariance_factor = {0.2F,0,0,0, 0,0.2F,0,0, 0,0,0.01F,0};
  temporal_capture->temporal = {conditional}; temporal_capture->local_time_range = {-1,1};
  const auto temporal_lit = renderer->render(temporal_capture,camera,true,0.0);
  if (!require(temporal_lit.error.isEmpty() && std::abs(temporal_lit.image.pixelColor(64,64).red()-77) <= 1,
      "Temporal captured lighting ignored conditional covariance normals")) return 1;
  effects.capture_power = 0.25F; const auto power = pixels(effects,captured);
  if (!require(std::abs(power.image.pixelColor(64,64).red()-std::lround(127.5*encoded(decoded(0.6)*0.25))) <= 1 &&
      !power.metrics.value("resolvedSourceReused").toBool(), "Captured lighting power or effect-only cache invalidation failed")) return 1;
  effects.capture_power = 1; effects.lights[0].vector = {0,0,1};
  if (!require(pixels(effects,captured).image.pixelColor(64,64).red() == 0,"Back-facing light illuminated an estimated plane")) return 1;
  // Known rotated covariance planes exercise all off-diagonal entries. The
  // light aligns with the exact input normal, independently of eigen ordering.
  for (const auto n : {QVector3D(0.3F,0.4F,-0.8660254F).normalized(), QVector3D(-0.6F,0.2F,-0.7745967F).normalized()}) {
    auto rotated = std::make_shared<GaussianSceneData>(*captured); auto& p = rotated->points[0];
    const auto cov = [&](int r,int c) { return (r == c ? 0.04F : 0.0F)-0.0399F*n[r]*n[c]; };
    p.covariance_a = {cov(0,0),cov(0,1),cov(0,2),cov(1,1)}; p.covariance_b = {cov(1,2),cov(2,2),0,0};
    effects.lights[0].vector = n;
    const auto result = pixels(effects,rotated);
    if (!require(result.error.isEmpty() && std::abs(result.image.pixelColor(64,64).red()-77) <= 1,"Rotated covariance normal lit the captured plane incorrectly")) return 1;
  }
  effects.lights[0].vector = {-1,0,-1};
  blocker.position = {-0.5F,0,3.5F}; blocker.size = {0.15F,0.15F,0.15F}; effects.objects = {blocker};
  const auto received_shadow = pixels(effects,captured);
  effects.shadow_strength = 0; const auto no_shadow = pixels(effects,captured);
  effects.shadow_strength = 0.5F; const auto half_shadow = pixels(effects,captured);
  const double shadow_oracle = 127.5*encoded(decoded(0.6)*std::sqrt(0.5)*0.5);
  if (!require(received_shadow.error.isEmpty() && received_shadow.image.pixelColor(64,64).red() == 0 &&
      no_shadow.image.pixelColor(64,64).red() > half_shadow.image.pixelColor(64,64).red() &&
      std::abs(half_shadow.image.pixelColor(64,64).red()-std::lround(shadow_oracle)) <= 1,"Object-to-capture shadow strength failed")) return 1;
  effects = GaussianSceneComposition{}; effects.ambient = 0; effects.haze_density = 0.2F; effects.haze_distance = 10; effects.haze_color = {0.4F,0.6F,0.8F};
  const auto fogged = pixels(effects,captured);
  const std::array<double,3> source_color{0.6,0.2,0.1}, fog_color{0.4,0.6,0.8};
  const auto fog_pixel = fogged.image.pixelColor(64,64); const std::array<int,3> actual_fog{fog_pixel.red(),fog_pixel.green(),fog_pixel.blue()};
  for (size_t channel = 0; channel < 3; ++channel) {
    const double f = decoded(fog_color[channel]);
    const double expected = 127.5*(encoded(f+(decoded(source_color[channel])-f)*std::exp(-0.8))+encoded(f*(1-std::exp(-2.0))));
    if (!require(std::abs(actual_fog[channel]-std::lround(expected)) <= 1,"Depth haze on transparent captured layers differs from independent Beer-Lambert oracle")) return 1;
  }
  effects.objects = {sphere()}; effects.objects[0].color = {0.6F,0.2F,0.1F}; effects.ambient = 1;
  const auto solid_fog = pixels(effects,captured);
  if (!require(solid_fog.error.isEmpty() && std::abs(solid_fog.image.pixelColor(64,64).red()-std::lround(255*encoded(decoded(0.4)+(decoded(0.6)-decoded(0.4))*std::exp(-0.5)))) <= 1,
      "Haze did not use actual solid intersection distance")) return 1;
  effects.capture_lighting = 1; effects.lights = {lamp}; effects.objects[0].position = {-0.5F,0,3.5F};
  const auto lit_fog = pixels(effects,captured);
  const auto tiled_fog = renderer->renderWithOptions(captured,camera,true,{},tiled_options);
  if (!require(lit_fog.error.isEmpty() && tiled_fog.error.isEmpty() && near_images(lit_fog.image,tiled_fog.image,0),"Relighting/haze changed at tile boundaries")) return 1;
  camera.photo.lens = {4,0.15F,0,0,0,32}; const auto lens_fog = renderer->render(captured,camera,true);
  const auto tiled_lens_fog = renderer->renderWithOptions(captured,camera,true,{},tiled_options);
  if (!require(lens_fog.error.isEmpty() && tiled_lens_fog.error.isEmpty() && near_images(lens_fog.image,tiled_lens_fog.image,1),"Aperture haze ray integration changed across tiles")) {
    int differences = 0, maximum = 0, mx = 0, my = 0;
    for (int y = 0; y < lens_fog.image.height(); ++y) for (int x = 0; x < lens_fog.image.width(); ++x) {
      const auto a = lens_fog.image.pixelColor(x,y), b = tiled_lens_fog.image.pixelColor(x,y);
      const int d = std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
      if (d > 0) ++differences;
      if (d > maximum) { maximum = d; mx = x; my = y; }
    }
    std::cerr << " errors=" << lens_fog.error.toStdString() << "/" << tiled_lens_fog.error.toStdString() << " differing=" << differences << " max=" << maximum << " at=" << mx << "," << my << "\n";
    return 1;
  }
  camera.photo = {}; effects = GaussianSceneComposition{};
  camera.composition.reset(); const auto unmodified = renderer->render(captured,camera,true);
  effects.capture_power = 0; effects.shadow_strength = 0; effects.haze_distance = 500; effects.haze_anisotropy = 0.5F;
  if (!require(pixels(effects,captured).image == unmodified.image,"Disabled extended settings changed captured appearance")) return 1;
  auto empty_view = std::make_shared<GaussianSceneData>(); empty_view->points = {splat(-4,{0,0,0})};
  effects.haze_density = 0.1F; effects.haze_distance = 5; effects.haze_color = {0.2F,0.2F,0.2F};
  effects.lights = {lamp}; effects.lights[0].vector = {0,0,1};
  const auto forward_fog = pixels(effects,empty_view);
  effects.lights[0].vector = {0,0,-1}; const auto backward_fog = pixels(effects,empty_view);
  if (!require(forward_fog.error.isEmpty() && backward_fog.error.isEmpty() && forward_fog.image.pixelColor(64,64).red() > backward_fog.image.pixelColor(64,64).red()+10,
      "Directional haze scattering lobe has wrong orientation")) return 1;
  const auto v3 = gaussian_scene_composition(effects.description());
  if (!require(v3 && v3->description() == effects.description(),"Extended composition roundtrip failed")) return 1;
  auto invalid_effect = effects; invalid_effect.haze_density = -0.1F;
  if (!require(!gaussian_scene_composition(invalid_effect.description()),"Negative haze density accepted")) return 1;
  invalid_effect = effects; invalid_effect.capture_lighting = 3;
  if (!require(!gaussian_scene_composition(invalid_effect.description()),"Unsupported captured-lighting mode accepted")) return 1;
  invalid_effect = effects; invalid_effect.haze_anisotropy = 1;
  if (!require(!gaussian_scene_composition(invalid_effect.description()),"Singular scattering phase accepted")) return 1;
  const auto effects_saved = save_gaussian_composition(temporary.path(),"artifact",effects,reopened.receipt);
  if (!require(effects_saved.error.isEmpty() && load_gaussian_composition(temporary.path(),"artifact").composition->description() == effects.description(),"Extended composition persistence failed")) return 1;
  // Reflected captured lighting must invalidate when lights change, even if the
  // probe anchor, artifact and camera remain identical.
  effects = GaussianSceneComposition{}; effects.objects = {sphere()}; effects.objects[0].position = {0,0,0}; effects.reflections = true;
  effects.capture_lighting = 1; effects.ambient = 0.25F; camera.position = {0,0,-3};
  const auto relit_reflection = pixels(effects,environment);
  effects.ambient = 0.5F; const auto changed_reflection = pixels(effects,environment);
  if (!require(relit_reflection.error.isEmpty() && changed_reflection.error.isEmpty() && !changed_reflection.metrics.value("reflectionProbeReused").toBool() &&
      relit_reflection.image != changed_reflection.image,"Reflections reused stale captured-scene lighting")) return 1;
  std::cout << "Captured lighting/haze: independent Lambert, rotated normal, power, received shadow, Beer-Lambert, solid depth, directional scattering, exact pinhole tiling and one-level aperture parity, disabled-default, persistence and reflection invalidation checks pass.\n";
  const QByteArray obj("# a two-sided plane with relative OBJ indices and smooth normals\nv -1 -1 3\nv 1 -1 3\nv 1 1 3\nv -1 1 3\nvn 0 0 -1\nf -4//1 -3//1 -2//1 -1//1\n");
  const auto imported_mesh = parse_gaussian_obj(obj);
  if (!require(imported_mesh.mesh && imported_mesh.mesh->triangles.size() == 2,"OBJ relative-index quad import failed")) return 1;
  for (const auto& invalid : {QByteArray("v nan 0 0\nf 1 1 1\n"), QByteArray("v 0 0 0\nf 0 1 1\n"), QByteArray("v 0 0 0\nf 1/1 1/1 1/1\n"), QByteArray("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n")})
    if (!require(!parse_gaussian_obj(invalid).mesh,"Malformed OBJ accepted")) return 1;
  const auto tiny_normal = parse_gaussian_obj("v -1 -1 3\nv 1 -1 3\nv 0 1 3\nvn 0 0 -0.000000001\nf 1//1 2//1 3//1\n");
  if (!require(tiny_normal.mesh && tiny_normal.mesh->triangles[0].normal0[2] < -0.99F,"Finite small OBJ normals were lost during normalization")) return 1;
  const auto canceled_import = parse_gaussian_obj(obj,std::make_shared<std::atomic_bool>(true));
  if (!require(!canceled_import.mesh && canceled_import.error.contains("canceled"),"Canceled OBJ import published geometry")) return 1;
  const auto distant_quad = parse_gaussian_obj("v 999999 999999 3\nv 1000001 999999 3\nv 1000001 1000001 3\nv 999999 1000001 3\nf 1 2 3 4\n");
  if (!require(distant_quad.mesh && distant_quad.mesh->triangles.size() == 2 && gaussian_mesh_hit(*distant_quad.mesh,{1000000,1000000,0},{0,0,1}) == 3,
      "Translated OBJ polygon lost its triangulation precision")) return 1;
  const auto concave = parse_gaussian_obj("v 0 0 3\nv 2 0 3\nv 2 1 3\nv 1 1 3\nv 1 2 3\nv 0 2 3\nf 1 2 3 4 5 6\n");
  if (!require(concave.mesh && concave.mesh->triangles.size() == 4 && gaussian_mesh_hit(*concave.mesh,{0.5F,1.5F,0},{0,0,1}) == 3 &&
      gaussian_mesh_hit(*concave.mesh,{1.5F,1.5F,0},{0,0,1}) > 1e29,"Concave OBJ face triangulation filled a missing corner")) return 1;
  if (!require(store_gaussian_mesh_asset(temporary.path(),imported_mesh).isEmpty(),"Mesh asset publication failed")) return 1;
  GaussianSceneObject mesh_object; mesh_object.id = "mesh"; mesh_object.name = "Imported plane"; mesh_object.kind = 2;
  mesh_object.mesh = imported_mesh.mesh; mesh_object.mesh_sha256 = imported_mesh.mesh->sha256; mesh_object.position = {}; mesh_object.size = {1,1,1}; mesh_object.color = {0,1,0};
  effects = GaussianSceneComposition{}; effects.objects = {mesh_object}; effects.ambient = 1;
  camera = GaussianCamera{}; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  const auto mesh_frame = pixels(effects,captured);
  if (!require(mesh_frame.error.isEmpty() && mesh_frame.image.pixelColor(64,64) == QColor(0,255,0) && mesh_frame.metrics.value("meshTriangleCount").toInt() == 2,
      "Imported triangle geometry did not occlude the rear splat")) { std::cerr << mesh_frame.error.toStdString(); return 1; }
  const auto mesh_focus = pick_gaussian_focus(*captured,camera,{64.5,64.5},{},{});
  if (!require(mesh_focus.value("ok").toBool() && std::abs(mesh_focus.value("focusDistance").toDouble()-3) < 1e-6,"Focus picking missed imported geometry")) return 1;
  const auto mesh_saved = save_gaussian_composition(temporary.path(),"artifact",effects,effects_saved.receipt);
  const auto mesh_reopened = load_gaussian_composition(temporary.path(),"artifact");
  if (!require(mesh_saved.error.isEmpty() && mesh_reopened.composition && mesh_reopened.composition->objects[0].mesh &&
      mesh_reopened.composition->description() == effects.description(),"Imported mesh persistence or immutable asset resolution failed")) return 1;
  auto malformed_mesh = effects.description(); auto malformed_objects = malformed_mesh.value("objects").toList(); auto malformed_object = malformed_objects[0].toMap();
  malformed_object["meshSha256"] = QString(64,'/'); malformed_objects[0] = malformed_object; malformed_mesh["objects"] = malformed_objects;
  if (!require(!gaussian_scene_composition(malformed_mesh),"Invalid mesh asset reference accepted")) return 1;
  // A front half-opacity splat still composites over a triangle surface.
  auto before_mesh = std::make_shared<GaussianSceneData>(); before_mesh->points = {splat(2,{1,0,0})};
  const auto mesh_layer = pixels(effects,before_mesh).image.pixelColor(64,64);
  if (!require(std::abs(mesh_layer.red()-128) <= 1 && std::abs(mesh_layer.green()-128) <= 1 && mesh_layer.blue() == 0,"Front splat transmittance over a mesh is incorrect")) return 1;
  // Nonuniform scale and rotation preserve the ray parameter and transform
  // normals by the inverse transpose. The center plane has z=3*2+1=7.
  effects.objects[0].size = {0.5F,1.5F,2}; effects.objects[0].position = {0,0,1}; effects.objects[0].rotation = {0,0,30};
  auto behind_mesh = std::make_shared<GaussianSceneData>(); behind_mesh->points = {splat(10,{1,0,0})};
  const auto transformed_mesh = pixels(effects,behind_mesh);
  const auto transformed_focus = pick_gaussian_focus(*behind_mesh,camera,{64.5,64.5},{},{});
  if (!require(transformed_mesh.error.isEmpty() && transformed_mesh.image.pixelColor(64,64) == QColor(0,255,0) &&
      transformed_focus.value("ok").toBool() && std::abs(transformed_focus.value("focusDistance").toDouble()-7) < 1e-5,"Transformed mesh intersection or focus depth failed")) return 1;
  effects.objects[0] = mesh_object; effects.objects[0].color = {1,1,1}; effects.ambient = 0; effects.lights = {lamp}; effects.lights[0].vector = {-1,0,-1}; effects.lights[0].intensity = 2;
  const auto lit_mesh = pixels(effects,behind_mesh);
  blocker.position = {-0.5F,0,2.5F}; blocker.size = {0.15F,0.15F,0.15F}; effects.objects.push_back(blocker);
  const auto shadow_mesh = pixels(effects,behind_mesh);
  if (!require(lit_mesh.error.isEmpty() && shadow_mesh.error.isEmpty() && lit_mesh.image.pixelColor(64,64).red() > 100 && shadow_mesh.image.pixelColor(64,64).red() == 0,
      "Imported mesh did not receive an analytic-object shadow")) return 1;
  // A separate imported triangle blocks an off-axis light without blocking
  // the camera. This also exercises multiple unique BVH roots and rebasing.
  const auto blocker_mesh = parse_gaussian_obj("v -0.8 -0.3 3.5\nv -0.2 -0.3 3.5\nv -0.5 0.3 3.5\nf 1 2 3\n");
  effects.objects = {mesh_object}; effects.objects[0].mesh = blocker_mesh.mesh; effects.objects[0].mesh_sha256 = blocker_mesh.mesh->sha256;
  effects.capture_lighting = 1; effects.shadow_strength = 1;
  const auto mesh_received = pixels(effects,captured);
  effects.shadow_strength = 0; const auto mesh_unshadowed = pixels(effects,captured);
  if (!require(mesh_received.error.isEmpty() && mesh_unshadowed.error.isEmpty() && mesh_received.image.pixelColor(64,64).red() == 0 &&
      mesh_unshadowed.image.pixelColor(64,64).red() > 40,"Imported mesh did not cast shadows onto captured splats")) return 1;
  effects.capture_lighting = 0; effects.ambient = 0; effects.objects = {floor}; effects.objects[0].color = {1,1,1};
  const auto analytic_receiver = pixels(effects,behind_mesh);
  auto imported_blocker = mesh_object; imported_blocker.id = "imported-blocker"; imported_blocker.mesh = blocker_mesh.mesh; imported_blocker.mesh_sha256 = blocker_mesh.mesh->sha256;
  effects.objects.push_back(imported_blocker); const auto analytic_mesh_shadow = pixels(effects,behind_mesh);
  if (!require(analytic_receiver.error.isEmpty() && analytic_mesh_shadow.error.isEmpty() && analytic_receiver.image.pixelColor(64,64).red() > 100 &&
      analytic_mesh_shadow.image.pixelColor(64,64).red() == 0,"Imported mesh did not cast shadows onto analytic objects")) { std::cerr << " lit=" << analytic_receiver.image.pixelColor(64,64).red() << " shadow=" << analytic_mesh_shadow.image.pixelColor(64,64).red(); return 1; }
  const auto self_occluding = parse_gaussian_obj("v -1 -1 4\nv 1 -1 4\nv 1 1 4\nv -1 1 4\nv -0.8 -0.3 3.5\nv -0.2 -0.3 3.5\nv -0.5 0.3 3.5\nf 1 2 3 4\nf 5 6 7\n");
  effects.objects = {mesh_object}; effects.objects[0].mesh = self_occluding.mesh; effects.objects[0].mesh_sha256 = self_occluding.mesh->sha256; effects.objects[0].color = {1,1,1};
  const auto mesh_self_shadow = pixels(effects,behind_mesh);
  if (!require(mesh_self_shadow.error.isEmpty() && mesh_self_shadow.image.pixelColor(64,64).red() == 0,"Imported geometry did not cast its own surface shadows")) return 1;
  effects.objects = {mesh_object}; effects.objects[0].mesh = blocker_mesh.mesh; effects.objects[0].mesh_sha256 = blocker_mesh.mesh->sha256;
  effects.objects[0].id = "mesh-blocker"; effects.objects.push_back(mesh_object); effects.objects.back().position = {2,0,1}; effects.objects.back().color = {0,0,1};
  effects.capture_lighting = 0; effects.ambient = 1; effects.haze_density = 0.03F; effects.reflections = true;
  const auto mesh_full = pixels(effects,behind_mesh); const auto mesh_tiles = renderer->renderWithOptions(behind_mesh,camera,true,{},tiled_options);
  if (!require(mesh_full.error.isEmpty() && mesh_tiles.error.isEmpty() && near_images(mesh_full.image,mesh_tiles.image,0) &&
      mesh_full.metrics.value("meshTriangleCount").toInt() == 3 &&
      mesh_full.metrics.value("reflectionProbeOrigin").toList() == QVariantList{-0.5F,0.0F,3.5F} &&
      mesh_full.image.pixelColor(47,64).green() > mesh_full.image.pixelColor(47,64).blue()+50 &&
      mesh_full.image.pixelColor(123,64).blue() > mesh_full.image.pixelColor(123,64).green()+50,"Multiple mesh assets, haze, reflection or tiled BVH roots failed")) return 1;
  effects.objects = {mesh_object}; effects.haze_density = 0; effects.reflections = false; effects.lights.clear();
  camera.photo.lens = {3,0.15F,0,0,0,32};
  const auto mesh_lens = pixels(effects,behind_mesh);
  const auto mesh_lens_tiles = renderer->renderWithOptions(behind_mesh,camera,true,{},tiled_options);
  if (!require(mesh_lens.error.isEmpty() && mesh_lens_tiles.error.isEmpty() && near_images(mesh_lens.image,mesh_lens_tiles.image,1),"Imported geometry aperture/tiling parity failed")) return 1;
  QByteArray grid_obj; int grid_index = 1;
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
    for (const auto& corner : {QVector2D(0,0),QVector2D(1,0),QVector2D(1,1),QVector2D(0,1)}) {
      const float px = -1+(static_cast<float>(x)+corner.x())/8, py = -1+(static_cast<float>(y)+corner.y())/8;
      grid_obj += "v "+QByteArray::number(px,'g',9)+" "+QByteArray::number(py,'g',9)+" "+QByteArray::number(3+0.25F*px,'g',9)+"\n";
    }
    grid_obj += "f "+QByteArray::number(grid_index)+" "+QByteArray::number(grid_index+1)+" "+QByteArray::number(grid_index+2)+" "+QByteArray::number(grid_index+3)+"\n"; grid_index += 4;
  }
  const auto grid = parse_gaussian_obj(grid_obj);
  if (!require(grid.mesh && grid.mesh->triangles.size() == 512 && grid.mesh->nodes.size() > 100,"Mesh BVH grid preparation failed")) return 1;
  effects.objects[0].mesh = grid.mesh; effects.objects[0].mesh_sha256 = grid.mesh->sha256; effects.objects[0].color = {0,0,1}; camera.photo = {};
  const auto grid_frame = pixels(effects,behind_mesh);
  const auto grid_focus = pick_gaussian_focus(*behind_mesh,camera,{84.5,64.5},{},{});
  if (!require(grid_frame.error.isEmpty() && grid_frame.image.pixelColor(84,64) == QColor(0,0,255) &&
      grid_focus.value("ok").toBool() && std::abs(grid_focus.value("focusDistance").toDouble()-3.0/(1-0.25*20.0/120)) < 1e-5,
      "Multilevel mesh BVH missed an independently defined sloping plane")) return 1;
  // Corrupting the project-local file must fail reopen rather than silently
  // substituting changed geometry under the frozen receipt hash.
  QFile corrupted(gaussian_mesh_asset_path(temporary.path(),mesh_object.mesh_sha256));
  if (!corrupted.open(QIODevice::WriteOnly|QIODevice::Append) || corrupted.write("# changed\n") != 10) return 1;
  corrupted.close();
  if (!require(!load_gaussian_composition(temporary.path(),"artifact").error.isEmpty(),"Changed mesh asset passed its integrity check")) return 1;
  std::cout << "Imported geometry: OBJ indices/normals/concave triangulation, asset integrity, transform/focus, solid/splat occlusion, mutual and captured shadows, multiple BVHs, reflection/haze and aperture tiling pass.\n";
  std::cout << "Scene objects: persistence, stale-writer protection, solid/splat occlusion, point-light falloff, hard shadows, focused optics, exact tiling, focus picking and cache invalidation pass.\n";
  std::cout << "Reflections: all six captured directions, view-only reuse, anchor invalidation and exact tiled parity pass.\n";
  return 0;
}
