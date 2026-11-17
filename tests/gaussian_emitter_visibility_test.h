#pragma once
#include "gaussian_focus_pick.h"
QImage read_gaussian_photo_texture(const photo_editor::desktop::GaussianFrame& frame);

static bool gaussian_primary_emitters_regression(const QString& proof) {
  const auto fail = [](const char* reason) { std::cerr << "Primary emitter: " << reason << '\n'; return false; };
  auto renderer = make_gaussian_scene_renderer();
  auto scene = std::make_shared<GaussianSceneData>(); auto background = point(); background.center_opacity[2] = 6; background.center_opacity[3] = 1; scene->points = {background};
  GaussianCamera camera; camera.image_size = {129,129}; camera.fx = camera.fy = 120; camera.cx = camera.cy = 64.5F;
  GaussianSceneComposition composition; composition.ambient = 10;
  GaussianSceneLight light; light.id = "source"; light.name = "Source"; light.kind = 2; light.vector = {0,0,3}; light.size = {2,1}; light.rotation = {180,0,0};
  light.color = {1,0.5F,0.25F}; light.intensity = 0.25F; light.camera_visible = true; composition.lights = {light};
  const auto render = [&] { camera.composition = std::make_shared<GaussianSceneComposition>(composition); return renderer->render(scene,camera,true); };
  const auto plane = render();
  const auto expected = [&](int axis) { return static_cast<int>(std::lround(255*encoded(linear(static_cast<int>(std::lround(light.color[axis]*255)))*light.intensity))); };
  // Constant source radiance and the pinhole projection of a 2 x 1 plane at Z=3.
  const auto center = plane.image.pixelColor(64,64);
  if (!plane.error.isEmpty() || std::abs(center.red()-expected(0)) > 1 || std::abs(center.green()-expected(1)) > 1 || std::abs(center.blue()-expected(2)) > 1 ||
      plane.image.pixelColor(30,50) != center || plane.image.pixelColor(109,64).red() != 0 || plane.image.pixelColor(64,40).red() != 0 ||
      plane.metrics.value("cameraVisibleEmitterCount").toInt() != 1 || plane.metrics.value("solidObjectCount").toInt() != 0 ||
      plane.metrics.contains("softLightModel")) return fail("plane silhouette/radiance or lights-only activation");
  const auto recipe = composition.description(); const auto parsed = gaussian_scene_composition(recipe);
  if (!parsed || parsed->description() != recipe || recipe.value("schema") != "looksy-scene-composition-v9") return fail("v9 recipe roundtrip");
  for (int fault = 0; fault < 6; ++fault) {
    auto invalid = recipe; auto lights = invalid.value("lights").toList(); auto lamp = lights[0].toMap();
    if (fault == 0) invalid["schema"] = "looksy-scene-composition-v8";
    if (fault == 1) lamp["cameraVisible"] = 1;
    if (fault == 2) lamp["extra"] = false;
    if (fault == 3) { GaussianSceneLight invalid_light; invalid_light.id = "source"; invalid_light.name = "Invalid point source"; invalid_light.kind = 1; lamp = invalid_light.description(); lamp["cameraVisible"] = true; }
    if (fault == 4) invalid.remove("alphaModel");
    if (fault == 5) invalid["environment"] = false;
    lights[0] = lamp; invalid["lights"] = lights;
    if (gaussian_scene_composition(invalid)) return fail("invalid source recipe accepted");
  }
  QTemporaryDir directory; const auto saved = save_gaussian_composition(directory.path(),"artifact",composition,{});
  const auto reopened = load_gaussian_composition(directory.path(),"artifact");
  if (!saved.composition || !reopened.composition || reopened.composition->description() != recipe) return fail("source persistence");
  auto false_recipe = recipe; auto false_lamps = false_recipe.value("lights").toList(); auto false_lamp = false_lamps[0].toMap();
  false_lamp["cameraVisible"] = false; false_lamps[0] = false_lamp; false_recipe["lights"] = false_lamps;
  auto legacy = composition; legacy.lights[0].camera_visible = false;
  const auto canonical = gaussian_scene_composition(false_recipe);
  if (!canonical || canonical->description() != legacy.description()) return fail("false visibility canonicalization");
  const auto focus = pick_gaussian_focus(*scene,camera,{64.5,64.5},{},{});
  if (!focus.value("ok").toBool() || std::abs(focus.value("focusDistance").toDouble()-3) > 1e-6) return fail("plane focus depth");
  composition.lights[0].camera_visible = false; const auto hidden = render();
  const auto behind_focus = pick_gaussian_focus(*scene,camera,{64.5,64.5},{},{});
  if (!hidden.error.isEmpty() || hidden.image.pixelColor(64,64).red() < 254 || behind_focus.value("focusDistance").toDouble() != 6) return fail("hidden source still occluded/picked");
  composition.lights[0] = light; composition.lights[0].intensity = 0;
  if (render().image.pixelColor(64,64).red() != 0) return fail("zero-power shown source lost its surface");
  auto nearer = light; nearer.id = "nearer"; nearer.kind = 3; nearer.vector = {0,0,2.5F}; nearer.radius = 0.2F; nearer.color = {0,0,1};
  composition.lights = {light,nearer}; const auto two_sources = render();
  std::swap(composition.lights[0],composition.lights[1]);
  if (two_sources.image.pixelColor(64,64).red() != 0 || two_sources.image.pixelColor(64,64).blue() < 136 ||
      !near(render().image,two_sources.image,0)) return fail("nearest-source selection depends on light order");
  composition.lights[0].vector.setZ(5);
  if (render().image.pixelColor(64,64) != center) return fail("farther source occluded nearer plane");
  composition.lights = {light};
  composition.lights[0] = light; composition.lights[0].rotation = {}; const auto back = render();
  if (back.image.pixelColor(64,64).red() != 0 || back.image.pixelColor(64,64).green() != 0) return fail("rectangle emitted backwards or lost opacity");
  composition.lights[0] = light; composition.lights[0].vector.setZ(0.005F);
  if (render().image.pixelColor(64,64).red() < 254) return fail("near clipping");
  composition.lights[0] = light; composition.lights[0].kind = 3; composition.lights[0].vector = {0,0,4}; composition.lights[0].radius = 1;
  const auto sphere = render(); const auto sphere_focus = pick_gaussian_focus(*scene,camera,{64.5,64.5},{},{});
  if (!sphere.error.isEmpty() || sphere.image.pixelColor(64,64) != center || sphere.image.pixelColor(94,64) != center || sphere.image.pixelColor(96,64).red() != 0 ||
      std::abs(sphere_focus.value("focusDistance").toDouble()-3) > 1e-6) return fail("sphere silhouette/radiance/focus");
  camera.position = {0,0,4};
  if (render().image.pixelColor(64,64).red() != 0 || std::abs(pick_gaussian_focus(*scene,camera,{64.5,64.5},{},{}).value("focusDistance").toDouble()-1) > 1e-6) return fail("one-sided sphere interior");
  camera.position = {}; composition.lights[0] = light;
  auto foreground = point(); foreground.center_opacity[2] = 2; foreground.center_opacity[3] = 0.5F;
  for (size_t axis = 0; axis < 3; ++axis) foreground.sh[axis] = ((axis == 0 ? 1.0F : 0.0F)-0.5F)/0.2820947918F;
  scene = std::make_shared<GaussianSceneData>(*scene); scene->points = {background,foreground}; const auto translucent = render();
  if (!translucent.error.isEmpty() || std::abs(translucent.image.pixelColor(64,64).red()-(127.5+center.red()*0.5)) > 1 ||
      std::abs(translucent.image.pixelColor(64,64).green()-center.green()*0.5) > 1) return fail("front-splat transmission/source depth");
  scene = std::make_shared<GaussianSceneData>(*scene); scene->points = {background}; GaussianSceneObject blocker; blocker.id = "blocker"; blocker.name = "Blocker"; blocker.position = {0,0,2}; blocker.size = {.2F,.2F,.2F}; blocker.color = {0,0,1};
  composition.objects = {blocker}; composition.ambient = 1;
  const auto blocked = render();
  if (blocked.image.pixelColor(64,64).red() != 0 || blocked.image.pixelColor(64,64).blue() < 254) return fail("solid occlusion");
  composition.objects[0].position.setZ(5);
  if (render().image.pixelColor(64,64) != center) return fail("solid behind source occluded it");
  composition.objects.clear(); composition.haze_density = 0.2F; composition.haze_color = {};
  const auto fogged = render();
  if (std::abs(fogged.image.pixelColor(64,64).red()-255*encoded(0.25*std::exp(-0.6))) > 1) return fail("distance haze");
  composition.haze_density = 0; composition.lights[0].size = {.04F,.04F}; composition.lights[0].intensity = 1; composition.lights[0].color = {1,1,1};
  background.center_opacity[3] = 0; scene = std::make_shared<GaussianSceneData>(*scene); scene->points = {background}; const auto pinhole = render();
  camera.photo.lens = {6,0.3F,0,0,0,256}; const auto defocused = render();
  if (!defocused.error.isEmpty() || defocused.image.pixelColor(64,64).red() >= pinhole.image.pixelColor(64,64).red()/2 ||
      defocused.image.pixelColor(69,64).red() <= 0) return fail("source aperture blur");
  GaussianRenderOptions tiled; tiled.export_tile_edge = 64; const auto tiles = renderer->renderWithOptions(scene,camera,true,{},tiled);
  if (!tiles.error.isEmpty() || !near(tiles.image,defocused.image,1) || read_gaussian_photo_texture(renderer->render(scene,camera,false)) != defocused.image) return fail("tiled/native aperture parity");
  auto hidden_optics = composition; hidden_optics.lights[0].camera_visible = false; camera.composition = std::make_shared<GaussianSceneComposition>(hidden_optics);
  const auto no_source = renderer->render(scene,camera,true);
  camera.composition = std::make_shared<GaussianSceneComposition>(composition); const auto restored = renderer->render(scene,camera,true);
  if (restored.metrics.value("resolvedSourceReused").toBool() || no_source.image == restored.image || !near(restored.image,defocused.image,0)) return fail("visibility cache invalidation");
  const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions progressive; progressive.progressive_frames = queue;
  const auto final = make_gaussian_scene_renderer()->renderWithOptions(scene,camera,false,{},progressive); const auto prefix = queue->takeLatest();
  auto partial_camera = camera; partial_camera.photo.lens.samples = 128;
  if (!final.error.isEmpty() || !prefix || prefix->metrics.value("apertureSamples").toInt() != 128 ||
      read_gaussian_photo_texture(*prefix) != make_gaussian_scene_renderer()->render(scene,partial_camera,true).image) return fail("progressive source prefix");
  camera.photo.lens.focus_distance = 3; const auto focused = render();
  if (!focused.error.isEmpty() || !near(focused.image,pinhole.image,1)) return fail("focused source changed radiance/silhouette");
  if (!proof.isEmpty()) {
    const auto path = QDir(proof).filePath("visible-sources"); if (!QDir().mkpath(path)) return fail("proof directory");
    for (const auto& entry : {std::pair<QString,QImage>{"rectangle",plane.image},{"sphere",sphere.image},{"back",back.image},{"front-transmission",translucent.image},{"solid-occlusion",blocked.image},{"haze",fogged.image},{"pinhole",pinhole.image},{"defocused",defocused.image}})
      if (!entry.second.save(path+"/"+entry.first+".png")) return fail("proof image");
    QFile file(path+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"recipe",recipe},{"planeCenter",center.name()},
        {"planeFocus",focus},{"sphereFocus",sphere_focus},{"checks",QStringList{"independent-projected-silhouettes","one-sided-radiance","near-clip","splat-transmission","solid-depth","haze","source-optics","native-tiles-progressive","cache","strict-v9-persistence"}},
        {"scope","Primary emitter visibility, not splat-cast shadows or emissive-material indirect illumination"}}).toJson();
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes) != bytes.size()) return fail("proof receipt");
  }
  std::cout << "Primary emitters: independent geometry/radiance/depth/focus, aperture/haze, strict recipes, persistence, cache and native/tiled/progressive parity pass\n";
  return true;
}
