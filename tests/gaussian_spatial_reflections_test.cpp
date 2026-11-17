#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <thread>

using namespace photo_editor::desktop;
#include "gaussian_spatial_reflections_dense.h"
namespace {
GaussianPoint colored(QVector3D center,QVector3D color,float opacity,float sigma = 0.15F) {
  GaussianPoint p; p.center_opacity = {center.x(),center.y(),center.z(),opacity};
  p.covariance_a = {sigma*sigma,0,0,sigma*sigma}; p.covariance_b = {0,sigma*sigma,0,0};
  for (size_t c = 0; c < 3; ++c) p.sh[c] = (color[static_cast<int>(c)]-0.5F)/0.2820947918F;
  return p;
}
double decode(double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); }
double encode(double c) { return c <= 0.0031308 ? 12.92*c : 1.055*std::pow(c,1/2.4)-0.055; }
double white_furnace(double nv,double roughness) {
  // Independent polar quadrature of D*G2/(4*N.V), F=1. This does not use the
  // shader's visible-normal sampler or its change-of-variables weight.
  constexpr int latitudes = 256, longitudes = 512; constexpr double pi = 3.14159265358979323846;
  const double a2 = std::pow(roughness,4), vx = std::sqrt(1-nv*nv);
  const double lv = (std::sqrt(1+a2*(1-nv*nv)/(nv*nv))-1)*0.5;
  double integral = 0;
  for (int t = 0; t < latitudes; ++t) {
    const double theta = (t+0.5)*pi/(2*latitudes), nl = std::cos(theta), sine = std::sin(theta);
    const double ll = (std::sqrt(1+a2*(1-nl*nl)/(nl*nl))-1)*0.5;
    for (int p = 0; p < longitudes; ++p) {
      const double phi = (p+0.5)*2*pi/longitudes, hx = vx+sine*std::cos(phi), hy = sine*std::sin(phi), hz = nv+nl;
      const double nh2 = hz*hz/(hx*hx+hy*hy+hz*hz), denominator = 1+nh2*(a2-1);
      const double distribution = a2/(pi*denominator*denominator), geometry = 1/(1+lv+ll);
      integral += distribution*geometry/(4*nv)*sine*pi/(2*latitudes)*2*pi/longitudes;
    }
  }
  return integral;
}
double rectangle_specular(QVector3D view,QVector3D source,double width,double height,double roughness) {
  constexpr int steps = 256; constexpr double pi = 3.14159265358979323846;
  const double nv = -view.z(), a2 = std::pow(roughness,4);
  const double lv = (std::sqrt(1+a2*(1-nv*nv)/(nv*nv))-1)*0.5;
  double integral = 0;
  // Independently integrate emitter area, rather than the shader's BSDF rays.
  for (int y = 0; y < steps; ++y) for (int x = 0; x < steps; ++x) {
    const double dx = source.x()+width*((x+0.5)/steps-0.5), dy = source.y()+height*((y+0.5)/steps-0.5), dz = source.z();
    const double r2 = dx*dx+dy*dy+dz*dz, r = std::sqrt(r2), nl = -dz/r;
    if (nl <= 0) continue;
    const double hx = dx/r+view.x(), hy = dy/r+view.y(), hz = dz/r+view.z();
    const double nh2 = hz*hz/(hx*hx+hy*hy+hz*hz), denominator = 1+nh2*(a2-1);
    const double d = a2/(pi*denominator*denominator), ll = (std::sqrt(1+a2*(1-nl*nl)/(nl*nl))-1)*0.5;
    integral += d/(1+lv+ll)/(4*nv)*nl/r2*width*height/(steps*steps);
  }
  return integral;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); const auto args = app.arguments();
  if (args.contains("--dense-project")) return gaussian_spatial_reflections_dense(args);
  const auto at = args.indexOf("--proof-dir"); const auto proof = at < 0 ? QString{} : args.value(at+1);
  if (at >= 0 && (!QFileInfo(proof).isAbsolute() || QFileInfo::exists(proof) || !QDir().mkpath(proof))) return 2;
  const auto fail = [](const char* reason) { std::cerr << "Spatial reflections: " << reason << '\n'; return 1; };
  GaussianSceneComposition composition; composition.ambient = 0; composition.reflections = true; composition.spatial_reflections = true; composition.light_samples = 16;
  GaussianSceneObject floor; floor.id = "mirror"; floor.name = "Mirror"; floor.kind = 1; floor.position = {}; floor.size = {3,3,0.01F};
  floor.color = {1,1,1}; floor.metallic = 1; floor.roughness = 0.04F; composition.objects = {floor};
  const auto parsed = gaussian_scene_composition(composition.description());
  if (!parsed || !parsed->spatial_reflections || parsed->description() != composition.description() || composition.description().value("schema") != "looksy-scene-composition-v11") return fail("v11 roundtrip");
  auto disabled = composition; disabled.reflections = false;
  const auto disabled_parsed = gaussian_scene_composition(disabled.description());
  if (!disabled_parsed || !disabled_parsed->spatial_reflections || disabled_parsed->reflections || disabled_parsed->needsProbe() || disabled_parsed->softLighting()) return fail("disabled reflections lost the selected mode or still require integration");
  for (const auto& invalid : {QVariant(true),QVariant(1),QVariant("unknown")}) {
    auto broken = composition.description(); broken["reflectionMode"] = invalid;
    if (gaussian_scene_composition(broken)) return fail("invalid reflection mode accepted");
  }
  QTemporaryDir project;
  if (!save_gaussian_composition(project.path(),"artifact",composition,{}).composition || !load_gaussian_composition(project.path(),"artifact").composition->spatial_reflections) return fail("persistence");
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.position = {2,0,-3}; const QVector3D hit(0,0,-0.01F);
  camera.forward = (hit-camera.position).normalized(); camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized(); camera.down = {0,1,0};
  QVector3D reflection = camera.forward; reflection.setZ(-reflection.z());
  auto scene = std::make_shared<GaussianSceneData>(); auto renderer = make_gaussian_scene_renderer();
  const auto render = [&](std::optional<double> time = {}) { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true,time); };
  QVariantList cases; int maximum_error = 0;
  const auto check = [&](QVector3D expected,const char* name,std::optional<double> time = {},int tolerance = 2) {
    const auto frame = render(time);
    if (!frame.error.isEmpty()) { std::cerr << frame.error.toStdString() << '\n'; return false; }
    if (frame.image.isNull()) return false;
    const auto color = frame.image.pixelColor(frame.image.width()/2,frame.image.height()/2); const int channels[]{color.red(),color.green(),color.blue()};
    int error = 0; for (int c = 0; c < 3; ++c) error = std::max(error,std::abs(channels[c]-qRound(255*expected[c])));
    maximum_error = std::max(maximum_error,error);
    cases.append(QVariantMap{{"case",name},{"maximumChannelError",error},{"metrics",frame.metrics}});
    if (!proof.isEmpty() && !frame.image.save(proof+"/"+name+".png")) return false;
    if (error > tolerance) { std::cerr << name << " RGB=" << channels[0] << ',' << channels[1] << ',' << channels[2] << " error=" << error << '\n'; return false; }
    return true;
  };
  scene->points = {colored(hit+reflection,{0.8F,0.2F,0.1F},0.8F)};
  if (!check({0.64F,0.16F,0.08F},"single-captured-ray")) return fail("single ray radiance");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored(hit+reflection,{0.8F,0.2F,0.1F},1)};
  if (!check(QVector3D(0.8F,0.2F,0.1F)*0.999F,"brush-secondary-alpha-cap")) return fail("spatial Brush alpha cap");
  composition.alpha_model = GaussianAlphaModel::Inria99;
  if (!check(QVector3D(0.8F,0.2F,0.1F)*0.99F,"inria-secondary-alpha-cap")) return fail("spatial original alpha cap");
  composition.alpha_model = GaussianAlphaModel::Brush999;
  scene = std::make_shared<GaussianSceneData>();
  for (int i = 0; i < 96; ++i) scene->points.push_back(colored(hit+reflection,i < 48 ? QVector3D(0.8F,0,0) : QVector3D(0,0,0.8F),0.04F));
  const float t = std::pow(0.96F,48.0F);
  if (!check({0.8F*(1-t),0,0.8F*t*(1-t)},"equal-depth-continuation")) return fail("tie ordering / batch continuation");
  scene = std::make_shared<GaussianSceneData>();
  for (int i = 0; i < 96; ++i) scene->points.push_back(colored(hit-reflection,i < 48 ? QVector3D(0.8F,0,0) : QVector3D(0,0,0.8F),0.04F,100));
  if (!check({0.8F*(1-t),0,0.8F*t*(1-t)},"zero-distance-peak-continuation")) return fail("clamped origin tie continuation");
  scene = std::make_shared<GaussianSceneData>();
  for (int i = 0; i < 400; ++i) scene->points.push_back(colored(hit+reflection,{0.8F,0,0},0.004F));
  if (!check({0.8F*(1-std::pow(0.996F,400.0F)),0,0},"four-hundred-transparent-particles")) return fail("silent ray-hit limit");
  scene = std::make_shared<GaussianSceneData>();
  scene->points = {colored(hit+reflection*2,{0.8F,0,0},0.5F),colored(hit+reflection,{0,0,0.8F},0.5F)};
  if (!check({0.2F,0,0.4F},"depth-order-over-source-order")) return fail("distance ordering");
  scene = std::make_shared<GaussianSceneData>();
  // A permutation of 96 distinct depths forces full-buffer replacements in
  // both traversals. Three color layers have an independent closed-form sum.
  for (int i = 0; i < 96; ++i) {
    const int rank = (i*37)%96;
    const QVector3D color = rank < 32 ? QVector3D(0.8F,0,0) : rank < 64 ? QVector3D(0,0.8F,0) : QVector3D(0,0,0.8F);
    scene->points.push_back(colored(hit+reflection*(0.6F+static_cast<float>(rank)*0.00625F),color,0.02F,0.2F));
  }
  const float layer_t = std::pow(0.98F,32.0F);
  if (!check(QVector3D(1,layer_t,layer_t*layer_t)*(0.8F*(1-layer_t)),"shuffled-depth-retained-buffer")) return fail("retained-buffer replacement and final ordering");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored(hit+reflection,{0.8F,0.2F,0.1F},0.8F)};
  auto anchor = floor; anchor.id = "distant-anchor"; anchor.position = {-10,10,5}; anchor.size = {0.1F,0.1F,0.1F};
  composition.objects = {anchor,floor};
  if (!check({0.64F,0.16F,0.08F},"spatial-independent-of-first-object")) return fail("surface origin");
  composition.spatial_reflections = false;
  if (!check({0,0,0},"probe-anchor-miss")) return fail("probe comparison");
  composition.spatial_reflections = true; composition.objects = {floor};
  for (int degree = 1; degree <= 3; ++degree) {
    scene = std::make_shared<GaussianSceneData>(); scene->sh_degree = degree;
    auto p = colored(hit+reflection,{0.5F,0.5F,0.5F},0.8F); const double z = reflection.z(), x = reflection.x();
    QVector3D expected(0.4F,0.4F,0.4F);
    if (degree == 1) { p.sh[9] = 0.5F; expected.setX(static_cast<float>(0.8*(0.5-0.4886025119*x*0.5))); }
    if (degree == 2) { p.sh[19] = 0.3F; expected.setY(static_cast<float>(0.8*(0.5+0.3153915653*(3*z*z-1)*0.3))); }
    if (degree == 3) { p.sh[38] = 0.3F; expected.setZ(static_cast<float>(0.8*(0.5+0.3731763326*z*(5*z*z-3)*0.3))); }
    scene->points = {p}; const auto name = QStringLiteral("directional-sh-%1").arg(degree).toLatin1();
    if (!check(expected,name.constData())) return fail("reflected SH direction");
  }
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored(hit+reflection,{0.8F,0.2F,0.1F},0.8F)};
  scene->time_origin_seconds = 100; scene->local_time_range = {0,2}; scene->temporal_sh_degree = 1;
  GaussianTemporalPoint motion; motion.velocity_time = {0.5F,0,0,0}; motion.normalization[0] = 1;
  motion.covariance_factor = {0.15F,0,0,0,0,0.15F,0,0,0,0,0.15F,0}; scene->temporal = {motion};
  GaussianTemporalAppearance appearance; appearance.sh[1] = 0.1F/0.2820947918F; scene->temporal_appearance = {appearance};
  if (!check({0.64F,0.24F,0.08F},"temporal-start",100)) return fail("temporal appearance start");
  const auto first_temporal = render(100);
  const double opacity = 0.8*std::exp(-0.5*0.5*0.5)*std::exp(-0.5*0.25*0.25*(1-reflection.x()*reflection.x())/(0.15*0.15));
  if (!check(QVector3D(0.8F,0.2F,0.1F)*static_cast<float>(opacity),"temporal-moved-faded",100.5) ||
      !check({0.64F,0.24F,0.08F},"temporal-revisit",100) || render(100).image != first_temporal.image) return fail("temporal mean, opacity, appearance or cache");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored(hit+reflection,{0.5F,0.25F,0.125F},0.5F)};
  composition.capture_lighting = 1; composition.capture_power = 2; composition.ambient = 0.25F;
  QVector3D lit; for (int c = 0; c < 3; ++c) lit[c] = static_cast<float>(0.5*encode(decode(c == 0 ? 0.5 : c == 1 ? 0.25 : 0.125)*0.5));
  if (!check(lit,"approximate-capture-lighting")) return fail("captured-color lighting");
  composition.capture_lighting = 0; composition.capture_power = 1; composition.ambient = 0;
  composition.haze_density = 0.2F; composition.haze_distance = 10; composition.haze_color = {0.1F,0.2F,0.3F};
  QVector3D fogged;
  for (int c = 0; c < 3; ++c) {
    const double fog = decode(composition.haze_color[c]), point = decode(c == 0 ? 0.5 : c == 1 ? 0.25 : 0.125);
    const double reflected = decode(0.5*encode(fog*(1-std::exp(-0.2))+point*std::exp(-0.2))+0.5*encode(fog*(1-std::exp(-2.0))));
    const double transmission = std::exp(-0.2*(camera.position-hit).length());
    fogged[c] = static_cast<float>(encode(fog*(1-transmission)+reflected*transmission));
  }
  if (!check(fogged,"two-segment-atmosphere")) return fail("reflected and camera-path atmosphere");
  composition.haze_density = 0; composition.haze_distance = 1000; composition.haze_color = {0.7F,0.8F,1};
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored({0,0,-100},{0,0,0},0)};
  auto object = floor; object.id = "reflected-object"; object.name = "Reflected object"; object.position = hit+reflection; object.size = {0.2F,0.2F,0.2F};
  object.color = {0.6F,0.2F,0.1F}; object.metallic = 0; object.roughness = 0.35F;
  composition.objects = {floor,object}; composition.ambient = 1;
  if (!check(object.color,"reflected-surface-ambient")) return fail("secondary surface");
  scene = std::make_shared<GaussianSceneData>();
  scene->points = {colored(hit+reflection*0.4F,{0,0,0.8F},0.5F,0.05F),colored(hit+reflection*2,{0.8F,0,0},0.8F)};
  if (!check({0.3F,0.1F,0.45F},"scan-before-opaque-reflected-surface")) return fail("secondary occlusion / finite scan segment");
  const auto endpoint_camera = camera;
  camera.position = {3,0,-2}; camera.forward = (hit-camera.position).normalized(); camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized();
  auto endpoint_ray = camera.forward; endpoint_ray.setZ(-endpoint_ray.z());
  composition.objects[1].position = hit+endpoint_ray;
  scene = std::make_shared<GaussianSceneData>();
  // At this grazing camera angle the broad particles' centers are behind the
  // primary mirror, so primary raster tails cannot contaminate the ray oracle.
  for (int i = 0; i < 96; ++i) scene->points.push_back(colored(hit+endpoint_ray*2,i < 48 ? QVector3D(0.8F,0,0) : QVector3D(0,0,0.8F),0.04F,1));
  const double endpoint = 1-0.2/std::abs(endpoint_ray.x());
  const float endpoint_t = static_cast<float>(std::pow(1-0.04*std::exp(-0.5*(2-endpoint)*(2-endpoint)),48));
  if (!check(QVector3D(0.8F*(1-endpoint_t),0,0.8F*endpoint_t*(1-endpoint_t))+object.color*(endpoint_t*endpoint_t),"finite-endpoint-peak-continuation")) return fail("clamped finite endpoint tie continuation");
  camera = endpoint_camera; composition.objects[1].position = object.position;
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored({0,0,-100},{0,0,0},0)};
  const auto obj_path = QDir(project.path()).filePath("reflected.obj"), mtl_path = QDir(project.path()).filePath("reflected.mtl"), image_path = QDir(project.path()).filePath("reflected.png");
  const QByteArray obj("mtllib reflected.mtl\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 1\nvt 1 1\nvt 1 0\nvt 0 0\nusemtl Surface\nf 1/1 2/2 3/3\nf 1/1 3/3 4/4\n");
  const QByteArray mtl("newmtl Surface\nKd 1 1 1\nPr 0.4\nPm 0\nmap_Kd reflected.png\n");
  const auto write_asset = [](const QString& path,const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); };
  QImage pattern(8,8,QImage::Format_RGBA8888); pattern.setColorSpace(QColorSpace::SRgb);
  for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) pattern.setPixelColor(x,y,x < 4 ? QColor(204,51,26) : QColor(0,0,204));
  if (!write_asset(obj_path,obj) || !write_asset(mtl_path,mtl) || !pattern.save(image_path)) return fail("texture fixture");
  const auto geometry = load_gaussian_obj(obj_path); if (!geometry.mesh) return fail("reflected OBJ");
  const auto assets = load_gaussian_obj_materials(obj_path,*geometry.mesh); if (!assets.assets) return fail("reflected MTL");
  auto textured = object; textured.kind = 2; textured.position = hit+reflection+QVector3D(0.175F,0,0); textured.size = {0.35F,0.35F,0.35F}; textured.color = {1,1,1};
  textured.mesh = geometry.mesh; textured.mesh_sha256 = geometry.mesh->sha256; textured.material_assets = assets.assets;
  textured.material_set_sha256 = assets.assets->sha256; textured.materials = assets.assets->materials;
  composition.objects = {floor,textured};
  if (!check({0.8F,0.2F,26.0F/255},"reflected-obj-uv-texture")) return fail("secondary OBJ/MTL color map");
  composition.objects = {floor,object};
  composition.ambient = 0; composition.objects[1].emission = 1;
  if (!check(object.color,"reflected-surface-emission")) return fail("secondary emission");
  composition.objects = {floor}; GaussianSceneLight light; light.id = "source"; light.name = "Source"; light.kind = 2;
  light.vector = hit+reflection; light.size = {0.8F,0.8F}; light.intensity = 0.3F; light.camera_visible = false;
  composition.lights = {light}; const float source_value = static_cast<float>(encode(0.3));
  if (!check({source_value,source_value,source_value},"reflected-hidden-primary-source")) return fail("finite-source specular visibility / double counting");
  composition.lights.clear(); scene = std::make_shared<GaussianSceneData>();
  scene->points = {colored({0,0,-10},{0.5F,0.5F,0.5F},0.99F,10000)};
  const auto full_camera = camera; camera = camera.resized({1,1}); composition.light_samples = 1024;
  for (const float roughness : {0.4F,0.7F,1.0F}) {
    composition.objects[0].roughness = roughness;
    const float value = static_cast<float>(encode(decode(0.495)*white_furnace(full_camera.forward.z(),roughness)));
    const auto name = QStringLiteral("rough-furnace-%1").arg(roughness).toLatin1();
    if (!check({value,value,value},name.constData(),{},3)) return fail("visible GGX / Smith white-furnace integral");
  }
  scene = std::make_shared<GaussianSceneData>(); scene->points = {colored({0,0,-100},{0,0,0},0)};
  composition.lights = {light}; composition.objects[0].roughness = 0.7F;
  const float finite_value = static_cast<float>(encode(light.intensity*rectangle_specular(-full_camera.forward,light.vector-hit,light.size.x(),light.size.y(),0.7)));
  if (!check({finite_value,finite_value,finite_value},"rough-finite-source-area-integral",{},3)) return fail("rough finite-source energy / double counting");
  composition.lights.clear();
  // Full-frame seeds, ray origins, aperture samples and texture footprints must
  // remain independent of export tiling. Use spatially varying captured color.
  camera = full_camera; composition.light_samples = 32; composition.objects = {floor};
  composition.objects[0].roughness = 0.4F;
  scene = std::make_shared<GaussianSceneData>();
  scene->points = {colored(hit+reflection,{0.8F,0.2F,0.1F},0.8F),colored(hit+reflection+QVector3D(0.3F,0.2F,0),{0.1F,0.6F,0.8F},0.6F)};
  const auto whole = render(); GaussianRenderOptions tiled; tiled.export_tile_edge = 32;
  const auto tiles = renderer->renderWithOptions(scene,camera,true,{},tiled);
  if (!whole.error.isEmpty() || !tiles.error.isEmpty() || whole.image != tiles.image) return fail("spatial rays changed across tile boundaries");
  camera.photo.lens = {3.6F,0.05F,0,0,0,16}; composition.haze_density = 0.05F;
  const auto optical = render(); const auto optical_tiles = renderer->renderWithOptions(scene,camera,true,{},tiled);
  int optical_error = 0;
  if (optical.image.size() != optical_tiles.image.size() || optical.image.isNull()) return fail("spatial aperture frame missing");
  for (int y = 0; y < optical.image.height(); ++y) for (int x = 0; x < optical.image.width(); ++x) {
    const auto a = optical.image.pixelColor(x,y), b = optical_tiles.image.pixelColor(x,y);
    optical_error = std::max({optical_error,std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
  }
  if (!optical.error.isEmpty() || !optical_tiles.error.isEmpty() || optical_error > 1) return fail("spatial aperture/haze tile mismatch");
  camera.photo = {}; composition.haze_density = 0;
  camera = camera.resized({513,513}); camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto canceled = std::make_shared<std::atomic_bool>(false), observed = std::make_shared<std::atomic_bool>(false);
  const auto batches = std::make_shared<std::atomic_int>(0);
  std::jthread stop([&] { QElapsedTimer timer; timer.start(); while (batches->load() == 0 && timer.elapsed() < 5000) std::this_thread::yield(); observed->store(batches->load() > 0); canceled->store(true); });
  GaussianRenderOptions cancel_options; cancel_options.canceled = canceled; cancel_options.lighting_batches_completed = batches;
  const auto interrupted = renderer->renderWithOptions(scene,camera,true,{},cancel_options); stop.join();
  if (!observed->load() || !interrupted.canceled || !interrupted.image.isNull() || interrupted.native_texture) return fail("spatial observed-batch cancellation published partial output");
  const QVariantMap receipt{{"passed",true},{"maximumChannelError",maximum_error},{"cases",cases},
      {"exactTiling",true},{"apertureHazeTileMaximumError",optical_error},{"cancellationObservedBatches",batches->load()},
      {"scope","independent captured-radiance and equal-depth continuation oracles; not complete photographic parity"}};
  if (!proof.isEmpty()) { QFile f(proof+"/receipt.json"); const auto b = QJsonDocument::fromVariant(receipt).toJson(); if (!f.open(QIODevice::WriteOnly) || f.write(b) != b.size()) return 2; }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n'; return 0;
}
