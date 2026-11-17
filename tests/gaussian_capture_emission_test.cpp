#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>
#include <thread>
#include <functional>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
namespace {
constexpr double pi = 3.14159265358979323846;
const QVector3D normal{float(std::sqrt(0.75)),0,-0.5F}, tangent{0.5F,0,float(std::sqrt(0.75))}, bitangent{0,1,0}, center{0,0,3};
const QVector3D base{0.6F,0.5F,0.4F};
GaussianPoint particle(QVector3D position,QVector3D color,QVector3D n,float sigma,float thin,float opacity = 1) {
  GaussianPoint p; p.center_opacity = {position.x(),position.y(),position.z(),opacity};
  const auto cov = [&](int a,int b) { return (a == b ? sigma*sigma : 0)+(thin*thin-sigma*sigma)*n[a]*n[b]; };
  p.covariance_a = {cov(0,0),cov(0,1),cov(0,2),cov(1,1)}; p.covariance_b = {cov(1,2),cov(2,2),0,0};
  for (int c = 0; c < 3; ++c) p.sh[static_cast<size_t>(c)] = (color[c]-0.5F)/0.2820947918F;
  return p;
}
double decode(double v) { return v <= 0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4); }
double encode(double v) { return v <= 0.0031308 ? 12.92*v : 1.055*std::pow(v,1/2.4)-0.055; }
double integral(const std::function<double(double)>& value,double maximum) {
  constexpr int n = 20000; double sum = value(0)+value(maximum);
  for (int i = 1; i < n; ++i) sum += value(maximum*i/n)*(i%2 ? 4 : 2);
  return sum*maximum/(3*n);
}
QColor expected(QVector3D emission,double fraction,double alpha = 0.999,double observer_transmittance = 1) {
  int rgb[3]{};
  for (int c = 0; c < 3; ++c) rgb[c] = qRound(255*alpha*std::clamp(encode(decode(base[c])*emission[c]*fraction*observer_transmittance),0.0,1.0));
  return {rgb[0],rgb[1],rgb[2]};
}
bool write(const QString& p,const QByteArray& data) { QFile f(p); return f.open(QIODevice::WriteOnly|QIODevice::NewOnly) && f.write(data) == data.size(); }
GaussianSceneObject sphere(float distance = 2,float radius = 1) {
  GaussianSceneObject o; o.id = "emitter"; o.name = "Emitting sphere"; o.position = center+normal*distance;
  o.size = {radius,radius,radius}; o.color = {1,0,0}; o.emission = 0.8F; return o;
}
GaussianSceneObject plane(const QString& root,float distance,float radius,bool emitting,float half_height = 0) {
  QByteArray bytes("mtllib panel.mtl\n");
  const auto p = center+normal*distance;
  for (const auto& uv : {QVector2D(-1,-1),QVector2D(1,-1),QVector2D(1,1),QVector2D(-1,1)}) {
    const auto v = p+tangent*(uv.x()*radius)+bitangent*(uv.y()*(half_height > 0 ? half_height : radius));
    bytes += "v "+QByteArray::number(v.x(),'g',9)+' '+QByteArray::number(v.y(),'g',9)+' '+QByteArray::number(v.z(),'g',9)+'\n';
  }
  bytes += "usemtl Panel\nf 1 2 3 4\n";
  const auto obj = QDir(root).filePath("panel.obj"), mtl = QDir(root).filePath("panel.mtl");
  if (!write(obj,bytes) || !write(mtl,emitting ? QByteArray("newmtl Panel\nKd 1 1 1\nKe 0.1 0.6 0.05\n") : QByteArray("newmtl Panel\nKd 0 0 0\nKe 0 0 0\n"))) return {};
  const auto mesh = load_gaussian_obj(obj); if (!mesh.mesh) return {};
  const auto materials = load_gaussian_obj_materials(obj,*mesh.mesh); if (!materials.assets) return {};
  if (!store_gaussian_mesh_asset(root,mesh).isEmpty() || !store_gaussian_material_assets(root,materials).isEmpty()) return {};
  GaussianSceneObject o; o.id = emitting ? "panel" : "mesh-blocker"; o.name = "Panel"; o.kind = 2; o.position = {}; o.size = {1,1,1};
  o.mesh = mesh.mesh; o.mesh_sha256 = mesh.mesh->sha256; o.materials = materials.assets->materials;
  o.material_set_sha256 = materials.assets->sha256; o.material_assets = materials.assets; return o;
}
}
int main(int argc,char** argv) {
  QGuiApplication app(argc,argv); QTemporaryDir temp;
  const auto args = app.arguments(); const auto proof_index = args.indexOf("--proof-dir"); const auto evidence = proof_index < 0 ? QString() : args.value(proof_index+1);
  if (!evidence.isEmpty() && (!QFileInfo(evidence).isAbsolute() || QFileInfo::exists(evidence) || !QDir().mkpath(evidence))) return 2;
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {particle(center,base,normal,0.2F,0.001F)};
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  GaussianSceneComposition composition; composition.objects = {sphere()}; composition.ambient = 0; composition.capture_lighting = 1;
  composition.capture_emission = true; composition.capture_emission_importance = args.contains("--importance"); composition.light_samples = 1024;
  auto renderer = make_gaussian_scene_renderer(); QVariantList cases; int maximum_error = 0;
  const auto render = [&] {
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    return renderer->render(std::make_shared<GaussianSceneData>(*scene),camera,true);
  };
  const auto check = [&](const char* name,const GaussianFrame& frame,QColor oracle,int tolerance = 1) {
    if (!frame.error.isEmpty() || frame.image.isNull()) { std::cerr << name << ": " << frame.error.toStdString() << '\n'; return false; }
    const auto actual = frame.image.pixelColor(32,32);
    const int error = std::max({std::abs(actual.red()-oracle.red()),std::abs(actual.green()-oracle.green()),std::abs(actual.blue()-oracle.blue())});
    maximum_error = std::max(maximum_error,error);
    cases.append(QVariantMap{{"name",name},{"actual",actual.name()},{"expected",oracle.name()},{"maximumChannelError",error},{"metrics",frame.metrics}});
    if (!evidence.isEmpty()) frame.image.save(QDir(evidence).filePath(QString::fromLatin1(name)+".png"));
    if (error > tolerance) { std::cerr << name << " actual " << actual.name().toStdString() << " expected " << oracle.name().toStdString() << " error " << error << '\n'; return false; }
    return true;
  };
  const auto description = composition.description();
  if (description.value("schema") != (composition.capture_emission_importance ? "looksy-scene-composition-v15" : "looksy-scene-composition-v14") || !gaussian_scene_composition(description) ||
      gaussian_scene_composition(description)->description() != description) return 1;
  for (const auto& bad : {QVariant(),QVariant(1),QVariant("true")}) {
    auto invalid = description; if (bad.isValid()) invalid["captureEmission"] = bad; else invalid.remove("captureEmission");
    if (gaussian_scene_composition(invalid)) return 1;
  }
  if (!check("sphere-lambert",render(),expected({0.8F,0,0},0.25))) return 1;
  composition.capture_power = 0.5F;
  if (!check("capture-power",render(),expected({0.8F,0,0},0.125))) return 1;
  composition.capture_power = 1; composition.objects[0] = sphere(4);
  if (!check("distance-solid-angle",render(),expected({0.8F,0,0},0.0625))) return 1;
  composition.objects[0] = sphere(); composition.scan_shadows = true;
  if (!check("receiver-excluded",render(),expected({0.8F,0,0},0.25))) return 1;
  auto invisible = particle({100,0,3},{0,0,0},normal,0.2F,0.001F,0.001F);
  scene->points.insert(scene->points.begin(),invisible);
  if (!check("source-id-not-primitive-id",render(),expected({0.8F,0,0},0.25))) return 1;
  scene->points.erase(scene->points.begin());
  scene->points.push_back(particle(center+normal*0.5F,{0,0,0},normal,0.15F,0.0005F,0.5F));
  const auto visibility_integral = [](int layers) { return integral([&](double u) {
    const auto q = 0.25*u/((1-u)*0.15*0.15+u*0.0005*0.0005);
    double alpha = 0.5*std::exp(-0.5*q); if (alpha < 1.0/255) alpha = 0;
    return std::pow(1-alpha,layers);
  },0.25); };
  if (!check("captured-occluder",render(),expected({0.8F,0,0},visibility_integral(1)),2)) return 1;
  scene->points.push_back(scene->points.back());
  if (!check("overlapping-captured-occluders",render(),expected({0.8F,0,0},visibility_integral(2)),2)) return 1;
  scene->points.resize(1); composition.scan_shadows = false;
  auto blocker = sphere(0.5F,0.4F); blocker.id = "blocker"; blocker.name = "Blocker"; blocker.color = {}; blocker.emission = 0;
  composition.objects.push_back(blocker);
  if (!check("solid-blocker",render(),expected({0.8F,0,0},0))) return 1;
  composition.shadow_strength = 0.25F;
  if (!check("shadow-strength",render(),expected({0.8F,0,0},0.25*0.75))) return 1;
  composition.shadow_strength = 1;
  const auto mesh_blocker_root = temp.filePath("blocker"); QDir().mkpath(mesh_blocker_root);
  const auto mesh_blocker = plane(mesh_blocker_root,0.5F,0.4F,false); if (!mesh_blocker.mesh) return 1;
  composition.objects[1] = mesh_blocker;
  if (!check("mesh-blocker",render(),expected({0.8F,0,0},0))) return 1;
  composition.objects.resize(1); composition.haze_density = 0.1F; composition.haze_color = {};
  const auto fog_integral = integral([](double u) { return std::exp(-0.1*(2*std::sqrt(1-u)-std::sqrt(std::max(0.0,1-4*u)))); },0.25);
  if (!check("emission-segment-haze",render(),expected({0.8F,0,0},fog_integral,0.999,std::exp(-0.3)),2)) return 1;
  composition.haze_density = 0;
  const auto panel_root = temp.filePath("emitter"); QDir().mkpath(panel_root);
  const auto panel = plane(panel_root,2,1,true); if (!panel.mesh) return 1;
  composition.objects = {panel};
  const double view_factor = 4/pi/std::sqrt(5.0)*std::atan(1/std::sqrt(5.0));
  if (!check("imported-material-emission",render(),expected({0.1F,0.6F,0.05F},view_factor),2)) return 1;
  const auto stored = save_gaussian_composition(panel_root,"capture-emission-fixture",composition,{});
  const auto reopened = load_gaussian_composition(panel_root,"capture-emission-fixture");
  if (!stored.error.isEmpty() || !reopened.composition || reopened.composition->description() != composition.description()) { std::cerr << "Emission persistence: " << stored.error.toStdString() << ' ' << reopened.error.toStdString() << '\n'; return 1; }
  auto box = sphere(); box.kind = 1; box.id = "box"; box.name = "Emitting box"; box.position = center+normal*2.01F; box.size = {1,1,0.01F};
  QMatrix3x3 axes;
  for (int row = 0; row < 3; ++row) { axes(row,0) = tangent[row]; axes(row,1) = -bitangent[row]; axes(row,2) = normal[row]; }
  box.rotation = QQuaternion::fromRotationMatrix(axes).toEulerAngles(); composition.objects = {box};
  if (!check("box-emission",render(),expected({0.8F,0,0},view_factor),2)) return 1;
  composition.objects[0].visible = false;
  if (!check("hidden-emitter",render(),Qt::black)) return 1;
  composition.objects = {sphere()}; composition.scan_shadows = true;
  const auto native_camera = [&] { auto c = camera; c.composition = std::make_shared<GaussianSceneComposition>(composition); return c; }();
  const auto direct = render();
  GaussianRenderOptions tiles; tiles.export_tile_edge = 32;
  const auto tiled = make_gaussian_scene_renderer()->renderWithOptions(scene,native_camera,true,{},tiles);
  const auto native = make_gaussian_scene_renderer()->render(scene,native_camera,false);
  if (!direct.error.isEmpty() || !tiled.error.isEmpty() || read_gaussian_photo_texture(native) != direct.image || tiled.image != direct.image || native.metrics.value("previewReadback").toBool()) { std::cerr << "Emission native/tile parity failed: " << direct.error.toStdString() << ' ' << tiled.error.toStdString() << '\n'; return 1; }
  auto progressive = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions options; options.progressive_frames = progressive;
  const auto full_native = make_gaussian_scene_renderer()->renderWithOptions(scene,native_camera,false,{},options);
  if (!progressive->takeLatest() || read_gaussian_photo_texture(full_native) != direct.image) { std::cerr << "Emission progressive final failed\n"; return 1; }
  composition.capture_emission = false;
  if (!check("disabled-default",render(),Qt::black)) return 1;
  composition.capture_emission = true; composition.capture_lighting = 0;
  if (!check("captured-appearance-preserved",render(),{qRound(base.x()*255*0.999),qRound(base.y()*255*0.999),qRound(base.z()*255*0.999)})) return 1;
  composition.capture_lighting = 1;
  auto moving = std::make_shared<GaussianSceneData>(*scene); GaussianTemporalPoint motion;
  motion.velocity_time = {normal.x()*0.2F,normal.y()*0.2F,normal.z()*0.2F,0}; motion.normalization[0] = 1;
  const QVector3D factors[]{tangent*0.2F,bitangent*0.2F,normal*0.001F};
  for (size_t column = 0; column < 3; ++column) for (size_t row = 0; row < 3; ++row)
    motion.covariance_factor[column*4+row] = factors[column][static_cast<int>(row)];
  moving->temporal = {motion}; moving->local_time_range = {0,2};
  auto moving_camera = native_camera; moving_camera.position = normal*0.2F;
  if (!check("temporal-receiver",make_gaussian_scene_renderer()->render(moving,moving_camera,true,1.0),expected({0.8F,0,0},1/(1.8*1.8),std::exp(-0.5)),2)) return 1;
  auto canceled = std::make_shared<std::atomic_bool>(false); auto batches = std::make_shared<std::atomic_int>(0);
  GaussianRenderOptions cancel_options; cancel_options.canceled = canceled; cancel_options.lighting_batches_completed = batches;
  std::jthread stopper([&](std::stop_token stop) { while (!stop.stop_requested() && batches->load() == 0) std::this_thread::sleep_for(std::chrono::microseconds(100)); if (!stop.stop_requested()) canceled->store(true); });
  const auto canceled_frame = make_gaussian_scene_renderer()->renderWithOptions(scene,native_camera,false,{},cancel_options);
  stopper.request_stop(); stopper.join();
  if (!canceled->load() || !canceled_frame.canceled || !canceled_frame.error.isEmpty() || canceled_frame.native_texture || !canceled_frame.image.isNull()) { std::cerr << "Emission cancellation failed\n"; return 1; }
  QVariantList convergence;
  if (args.contains("--importance")) {
    auto invalid = composition.description(); invalid["captureEmissionSampling"] = "unknown";
    if (gaussian_scene_composition(invalid)) return 1;
    auto explicit_cosine = composition.description(); explicit_cosine["captureEmissionSampling"] = "cosine"; explicit_cosine["captureEmission"] = false;
    const auto retained_cosine = gaussian_scene_composition(explicit_cosine);
    if (!retained_cosine || retained_cosine->description() != explicit_cosine) { std::cerr << "Disabled explicit cosine preference lost\n"; return 1; }
    composition.scan_shadows = false; composition.light_samples = 1024;
    composition.objects = {sphere(),sphere()}; composition.objects[1].id = "duplicate-bound";
    if (!check("overlapping-emitter-bounds",render(),expected({0.8F,0,0},0.25))) return 1;
    const float angle = float(20*pi/180);
    auto red = sphere(2,0.2F), green = red; green.id = "green"; green.color = {0,1,0}; red.emission = green.emission = 8;
    red.position = center+(normal*std::cos(angle)+tangent*std::sin(angle))*2;
    green.position = center+(normal*std::cos(angle)-tangent*std::sin(angle))*2;
    composition.objects = {red,green};
    if (!check("disjoint-colored-emitters",render(),expected({8,8,0},0.01*std::cos(angle)),2)) return 1;
    const auto broad_root = temp.filePath("broad-emitter"); QDir().mkpath(broad_root);
    const auto broad = plane(broad_root,0.5F,0.1F,true,2); if (!broad.mesh) return 1;
    composition.objects = {broad};
    const double a = 0.2, b = 4;
    const double broad_factor = 2/pi*(a/std::sqrt(1+a*a)*std::atan(b/std::sqrt(1+a*a))+b/std::sqrt(1+b*b)*std::atan(a/std::sqrt(1+b*b)));
    if (!check("receiver-inside-emitter-bound",render(),expected({0.1F,0.6F,0.05F},broad_factor),2)) return 1;
    const auto small_root = temp.filePath("small-mesh-emitter"); QDir().mkpath(small_root);
    auto small_panel = plane(small_root,2,0.05F,true); if (!small_panel.mesh) return 1;
    for (auto& material : small_panel.materials) if (material.emission.lengthSquared() > 0) material.emission = {100,0,0};
    for (const bool mesh_source : {false,true}) {
      composition.scan_shadows = false; composition.objects = {sphere(2,0.05F)}; composition.objects[0].emission = 100;
      if (mesh_source) composition.objects = {small_panel};
      const double ratio = 0.05/2, panel_factor = 4/pi*ratio/std::sqrt(1+ratio*ratio)*std::atan(ratio/std::sqrt(1+ratio*ratio));
      const auto oracle = expected({100,0,0},mesh_source ? panel_factor : ratio*ratio);
      for (const int samples : {8,16,64}) {
        int legacy_error = 0, targeted_error = 0, targeted_maximum = 0;
        composition.light_samples = std::max(16,samples);
        for (int seed = 0; seed < 16; ++seed) {
          auto fixture = std::make_shared<GaussianSceneData>();
          for (int j = 0; j < seed; ++j) fixture->points.push_back(invisible);
          fixture->points.push_back(particle(center,base,normal,0.2F,0.001F));
          for (const bool importance : {false,true}) {
            composition.capture_emission_importance = importance;
            auto c = camera; c.composition = std::make_shared<GaussianSceneComposition>(composition);
            GaussianRenderOptions quality; quality.interactive = samples == 8;
            const auto frame = renderer->renderWithOptions(fixture,c,true,{},quality);
            if (!frame.error.isEmpty() || frame.image.isNull()) return 1;
            const int error = std::abs(frame.image.pixelColor(32,32).red()-oracle.red());
            if (importance) { targeted_error += error; targeted_maximum = std::max(targeted_maximum,error); }
            else legacy_error += error;
            if (!evidence.isEmpty() && seed == 0) frame.image.save(QDir(evidence).filePath(QStringLiteral("small-%1-%2-%3.png").arg(mesh_source ? "mesh" : "sphere").arg(samples).arg(importance ? "targeted" : "legacy")));
          }
        }
        convergence.append(QVariantMap{{"source",mesh_source ? "mesh" : "sphere"},{"samples",samples},{"seedCount",16},{"legacyAbsoluteChannelError",legacy_error},{"targetedAbsoluteChannelError",targeted_error},{"targetedMaximumChannelError",targeted_maximum},{"oracle",oracle.name()}});
        // Eight is the live draft; 64 is the default final quality. Require
        // substantial improvement at every budget and close default-quality
        // agreement, without treating a draft as a converged final image.
        if ((samples == 64 && targeted_maximum > 2) || targeted_error >= legacy_error/4) { std::cerr << "Small emitter convergence failed: " << samples << ' ' << targeted_error << ' ' << legacy_error << ' ' << targeted_maximum << '\n'; return 1; }
      }
    }
    composition.capture_emission_importance = true; composition.reflections = true;
    auto probe_scene = std::make_shared<GaussianSceneData>(*scene);
    const auto probe_render = [&](bool interactive) {
      auto c = camera; c.composition = std::make_shared<GaussianSceneComposition>(composition);
      GaussianRenderOptions quality; quality.interactive = interactive;
      return renderer->renderWithOptions(probe_scene,c,true,{},quality);
    };
    const auto final_probe = probe_render(false), reused_probe = probe_render(false), live_probe = probe_render(true), restored_probe = probe_render(false);
    if (!final_probe.error.isEmpty() || !reused_probe.error.isEmpty() || !live_probe.error.isEmpty() || !restored_probe.error.isEmpty() ||
        final_probe.metrics.value("reflectionProbeLightingSamples").toInt() != 64 || !reused_probe.metrics.value("reflectionProbeReused").toBool() ||
        live_probe.metrics.value("reflectionProbeLightingSamples").toInt() != 8 || live_probe.metrics.value("reflectionProbeReused").toBool() ||
        restored_probe.metrics.value("reflectionProbeLightingSamples").toInt() != 64 || restored_probe.metrics.value("reflectionProbeReused").toBool()) {
      std::cerr << "Emission probe sample quality/cache failed\n"; return 1;
    }
    composition.capture_emission_importance = false; composition.capture_emission_sampling_explicit = true;
    const auto cosine_probe = probe_render(false);
    if (!cosine_probe.error.isEmpty() || cosine_probe.metrics.value("reflectionProbeLightingSamples").toInt() != 64 || cosine_probe.metrics.value("reflectionProbeReused").toBool()) return 1;
  }
  if (!evidence.isEmpty()) {
    const QVariantMap receipt{{"passed",true},{"cases",cases},{"maximumChannelError",maximum_error},{"strictCompositionPersistence",true},{"compositionSchema",description.value("schema")},{"probeQualityCacheQualified",args.contains("--importance")},
        {"nativeReadbackAndTilesExact",true},{"progressiveFinalExact",true},{"canceledAfterCaptureBatch",true},{"smallEmitterConvergence",convergence},
        {"scope","Independent analytic Lambert solid angles, quadrature visibility/haze, material import/persistence, native/tile/progressive and cancellation; captured-color relighting remains approximate"}};
    if (!write(QDir(evidence).filePath("receipt.json"),QJsonDocument::fromVariant(receipt).toJson())) return 2;
  }
  std::cout << "Captured emission: " << cases.size() << " rendered cases, maximum error " << maximum_error << "/255; persistence, native/tile/progressive and cancellation pass\n";
  return 0;
}
