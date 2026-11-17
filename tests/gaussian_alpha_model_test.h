#pragma once
#include "gaussian_focus_pick.h"
static bool gaussian_alpha_model_regression(const QString& evidence = {}) {
  const auto fail = [](const char* reason) { std::cerr << "Alpha model: " << reason << '\n'; return false; };
  GaussianSceneComposition original; original.alpha_model = GaussianAlphaModel::Inria99;
  const auto parsed = gaussian_scene_composition(original.description());
  if (!parsed || parsed->description() != original.description() || original.description().value("schema") != "looksy-scene-composition-v8") return fail("roundtrip");
  for (int fault = 0; fault < 7; ++fault) {
    auto value = original.description();
    if (fault == 0) value["alphaModel"] = "unknown";
    if (fault == 1) value["alphaModel"] = 1;
    if (fault == 2) value.remove("alphaModel");
    if (fault == 3) value["extra"] = 1;
    if (fault == 4) value["schema"] = "looksy-scene-composition-v7";
    if (fault == 5) value.remove("environment");
    if (fault == 6) value["environment"] = true;
    if (gaussian_scene_composition(value)) return fail("invalid recipe accepted");
  }
  auto neutral = original.description(); neutral["alphaModel"] = "brush-alpha-0999-v1";
  const auto legacy = gaussian_scene_composition(neutral);
  if (!legacy || legacy->description() != GaussianSceneComposition{}.description()) return fail("neutral canonicalization");
  QTemporaryDir project; const auto saved = save_gaussian_composition(project.path(),"artifact",original,{});
  const auto reopened = load_gaussian_composition(project.path(),"artifact");
  if (!saved.composition || !reopened.composition || reopened.composition->alpha_model != GaussianAlphaModel::Inria99) return fail("persistence");
  auto renderer = make_gaussian_scene_renderer();
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  auto scene = std::make_shared<GaussianSceneData>();
  auto front = point(2,{1,0,0}); front.center_opacity[3] = 0.9F;
  auto back = point(4,{0,1,0}); back.center_opacity[3] = 1; scene->points = {front,back};
  const auto brush = renderer->render(scene,camera,true);
  camera.composition = std::make_shared<GaussianSceneComposition>(original);
  const auto inria = renderer->render(scene,camera,true);
  // With T=.1, the capped .999 sample crosses the existing termination limit;
  // .99 retains a .099 green contribution. This is a large, observable case.
  if (!brush.error.isEmpty() || !inria.error.isEmpty() || brush.image.pixelColor(32,32).green() != 0 ||
      std::abs(inria.image.pixelColor(32,32).green()-25) > 1 ||
      std::abs(inria.image.pixelColor(32,32).red()-230) > 1 ||
      inria.metrics.value("alphaModel") != "inria-alpha-099-v1") return fail("independent high-opacity/termination oracle");
  auto focus_scene = std::make_shared<GaussianSceneData>(*scene); focus_scene->points[0].center_opacity[3] = 0.499F;
  const auto inria_focus = pick_gaussian_focus(*focus_scene,camera,{32.5,32.5},{},{});
  camera.composition.reset(); const auto brush_focus = pick_gaussian_focus(*focus_scene,camera,{32.5,32.5},{},{});
  if (!inria_focus.value("ok").toBool() || !brush_focus.value("ok").toBool() ||
      inria_focus.value("focusDistance").toDouble() != 4 || brush_focus.value("focusDistance").toDouble() != 4 ||
      inria_focus.value("opacity").toDouble() >= brush_focus.value("opacity").toDouble())
    return fail("opacity-median focus convention");
  camera.photo.lens = {3,0.1F,7,15,0.2F,32};
  const auto warm = renderer->render(scene,camera,true);
  camera.composition = std::make_shared<GaussianSceneComposition>(original);
  const auto classic = renderer->render(scene,camera,true);
  if (!warm.error.isEmpty() || !classic.error.isEmpty() || classic.metrics.value("resolvedSourceReused").toBool() ||
      renderer->render(scene,camera,true).image != classic.image ||
      !renderer->render(scene,camera,true).metrics.value("resolvedSourceReused").toBool()) return fail("source-cache profile boundary");
  GaussianRenderOptions tiled; tiled.export_tile_edge = 32;
  const auto tile_frame = renderer->renderWithOptions(scene,camera,true,{},tiled);
  const auto native = read_gaussian_photo_texture(renderer->render(scene,camera,false));
  if (!tile_frame.error.isEmpty() || tile_frame.image.size() != classic.image.size() || native != classic.image) return fail("tiled/native profile output");
  int tile_error = 0;
  for (int y = 0; y < classic.image.height(); ++y) for (int x = 0; x < classic.image.width(); ++x) {
    const auto a = tile_frame.image.pixelColor(x,y), b = classic.image.pixelColor(x,y);
    tile_error = std::max({tile_error,std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
  }
  if (tile_error > 1) { std::cerr << "Alpha tile error: " << tile_error << '\n'; return fail("tiled profile parity"); }
  const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions progressive; progressive.progressive_frames = queue;
  const auto final = make_gaussian_scene_renderer()->renderWithOptions(scene,camera,false,{},progressive); const auto partial = queue->takeLatest();
  auto prefix = camera; prefix.photo.lens.samples = 16;
  if (!final.error.isEmpty() || !partial || partial->metrics.value("alphaModel") != "inria-alpha-099-v1" ||
      read_gaussian_photo_texture(*partial) != make_gaussian_scene_renderer()->render(scene,prefix,true).image) return fail("progressive profile prefix");
  // A fully opaque captured marker supplies a known reflected value and checks
  // that probe captures use the selected cap, including pure-alpha recipes.
  auto env = std::make_shared<GaussianSceneData>(); auto marker = point(-5,{1,1,1}); marker.center_opacity[3] = 1;
  marker.covariance_a = {1e8F,0,0,1e8F}; marker.covariance_b = {0,1e8F,0,0}; env->points = {marker};
  GaussianSceneObject sphere; sphere.id = "mirror"; sphere.name = "Mirror"; sphere.position = {}; sphere.color = {1,1,1}; sphere.metallic = 1; sphere.roughness = 0.04F;
  GaussianSceneComposition reflected; reflected.objects = {sphere}; reflected.reflections = true; reflected.ambient = 0;
  camera = {}; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F; camera.position = {0,0,-3};
  camera.composition = std::make_shared<GaussianSceneComposition>(reflected);
  const auto brush_probe = renderer->render(env,camera,true);
  reflected.alpha_model = GaussianAlphaModel::Inria99; camera.composition = std::make_shared<GaussianSceneComposition>(reflected);
  const auto classic_probe = renderer->render(env,camera,true);
  if (!brush_probe.error.isEmpty() || !classic_probe.error.isEmpty() || classic_probe.metrics.value("reflectionProbeReused").toBool() ||
      brush_probe.image.pixelColor(32,32).red() < 254 || std::abs(classic_probe.image.pixelColor(32,32).red()-252) > 1 ||
      !renderer->render(env,camera,true).metrics.value("reflectionProbeReused").toBool()) return fail("probe profile propagation/invalidation");
  auto invalid = original; invalid.alpha_model = static_cast<GaussianAlphaModel>(2); camera.composition = std::make_shared<GaussianSceneComposition>(invalid);
  if (renderer->render(scene,camera,true).error.isEmpty()) return fail("invalid C++ model accepted");
  if (!evidence.isEmpty()) {
    if (QFileInfo::exists(evidence) || !QDir().mkpath(evidence) || !brush.image.save(evidence+"/brush.png") || !inria.image.save(evidence+"/inria.png")) return fail("evidence images");
    QFile file(evidence+"/receipt.json"); const QVariantMap receipt{{"passed",true},{"brushCenter",brush.image.pixelColor(32,32).name()},
        {"inriaCenter",inria.image.pixelColor(32,32).name()},{"brushFocus",brush_focus},{"inriaFocus",inria_focus},{"maximumTileCodeError",tile_error},
        {"scope","independent alpha/termination and focus oracles, strict persistence, cache, probe, progressive and tile/native parity; not complete original-3DGS raster equivalence"}};
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return fail("receipt");
  }
  std::cout << "Alpha models: independent high-opacity/focus oracles, persistence, cache, probes, tiles and progressive pixels pass\n";
  return true;
}
