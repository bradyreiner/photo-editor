#pragma once
#include "gaussian_autofocus.h"
static bool gaussian_autofocus_regression(const QString& output = {}) {
  const auto fail = [](const char* stage) { std::cerr << "Autofocus: " << stage << '\n'; return false; };
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.photo.lens = {1,0.2F,0,0,0,32}; camera.photo.lens.auto_focus = true;
  auto scene = std::make_shared<GaussianSceneData>(); auto p = point(3,{1,0.5F,0.2F}); p.center_opacity[3] = 0.999F;
  scene->points = {p}; auto renderer = make_gaussian_scene_renderer();
  const auto focus = [&](const GaussianFrame& frame) { return frame.metrics.value("autofocus").toMap().value("resolvedFocusDistance").toDouble(); };
  const auto resolved = [&](const GaussianFrame& frame) { return frame.metrics.value("resolvedCamera").toMap(); };
  auto manual = camera; manual.photo.lens.auto_focus = false; manual.photo.lens.focus_distance = 3;
  const auto first = renderer->render(scene,camera,true); const auto actual = resolved(first);
  if (!first.error.isEmpty() || actual != manual.description() || focus(first) != 3 ||
      !first.metrics.value("autofocus").toMap().value("locked").toBool() || first.metrics.value("camera").toMap() != camera.description()) return fail("initial centre lock and authored/resolved separation");
  const auto independent = make_gaussian_scene_renderer()->render(scene,manual,true);
  if (first.image != independent.image || renderer->render(scene,camera,true).image != first.image) return fail("independent manual-camera pixel parity");
  auto moved = camera; moved.position.setZ(1);
  const auto second = renderer->render(scene,moved,true); if (focus(second) != 2 || first.image == second.image) return fail("movement updates focus");
  auto moved_actual = moved; moved_actual.photo.lens.auto_focus = false; moved_actual.photo.lens.focus_distance = 2;
  if (resolved(second) != moved_actual.description() || make_gaussian_scene_renderer()->render(scene,moved_actual,true).image != second.image) return fail("resolved camera exactly replays movement");
  auto miss = camera; miss.position.setX(10);
  const auto fallback = renderer->render(scene,miss,true);
  if (!fallback.error.isEmpty() || focus(fallback) != 1 || fallback.metrics.value("autofocus").toMap().value("locked").toBool() ||
      fallback.metrics.value("autofocus").toMap().value("reason").toString().isEmpty()) return fail("empty centre uses authored fallback");
  auto too_close = std::make_shared<GaussianSceneData>(); auto close = p; close.center_opacity[2] = 0.1F; too_close->points = {close};
  const auto extreme_focus=renderer->render(too_close,camera,true);
  if (!extreme_focus.error.isEmpty() || std::abs(focus(extreme_focus)-0.1)>1e-6 ||
      !extreme_focus.metrics.value("autofocus").toMap().value("locked").toBool())
    return fail("experimental pupil can focus inside the former aperture limit");
  close.center_opacity[2]=0.02F; too_close=std::make_shared<GaussianSceneData>(); too_close->points={close};
  const auto range = renderer->render(too_close,camera,true);
  if (!range.error.isEmpty() || focus(range) != 1 || range.metrics.value("autofocus").toMap().value("locked").toBool()) return fail("lens-range failure preserves fallback and aperture");
  // This subpixel foreground crosses opacity 0.5 only with the screen-pixel
  // covariance floor. Autofocus must use the unfiltered ray at every output size.
  auto subpixel = std::make_shared<GaussianSceneData>(*scene); auto foreground = point(1,{1,1,1}); foreground.center_opacity = {0.01F,0,1,0.999F};
  foreground.covariance_a = {1e-6F,0,0,1e-6F}; foreground.covariance_b = {0,1e-12F,0,0}; subpixel->points.push_back(foreground);
  if (pick_gaussian_focus(*subpixel,camera,QPointF(32.5,32.5),{},{}).value("focusDistance").toDouble() != 1) return fail("resolution-sensitivity fixture");
  for (const int edge : {65,129,257}) if (focus(renderer->render(subpixel,camera.resized({edge,edge}),true)) != 3) return fail("resolution-independent centre focus");
  auto composition = std::make_shared<GaussianSceneComposition>(); GaussianSceneObject sphere; sphere.id = "focus-sphere"; sphere.name = "Focus sphere";
  sphere.position = {0,0,2}; sphere.size = {0.2F,0.2F,0.2F}; composition->objects = {sphere}; composition->ambient = 1;
  auto hybrid = camera; hybrid.composition = composition;
  const auto solid = renderer->render(scene,hybrid,true);
  if (!solid.error.isEmpty() || std::abs(focus(solid)-1.8) > 1e-5) return fail("added solid occludes captured centre");
  auto hidden = std::make_shared<GaussianSceneComposition>(*composition); hidden->objects[0].visible = false; hybrid.composition = hidden;
  if (focus(renderer->render(scene,hybrid,true)) != 3) return fail("composition change re-evaluates centre");
  auto temporal = std::make_shared<GaussianSceneData>(*scene); GaussianTemporalPoint motion;
  motion.velocity_time = {0,0,1,0}; motion.normalization[0] = 1; motion.covariance_factor = {0.1F,0,0,0,0,0.1F,0,0,0,0,0.1F,0};
  temporal->temporal = {motion}; temporal->local_time_range = {0,1};
  const auto timed = renderer->render(temporal,camera,true,0.5);
  if (!timed.error.isEmpty() || focus(timed) != 3.5) return fail("temporal surface updates focus");
  GaussianPhysicalCameraSettings physical; physical.metres_per_scene_unit = 0.5;
  const auto physical_camera = make_gaussian_physical_camera(camera,camera.image_size,physical);
  if (!physical_camera.camera) return fail("physical fixture");
  const auto physical_frame = renderer->render(scene,*physical_camera.camera,true);
  auto physical_manual = *physical_camera.camera; physical_manual.photo.lens.auto_focus = false; physical.focus_distance_m = 1.5;
  const auto physical_actual = make_gaussian_physical_camera(physical_manual,camera.image_size,physical);
  if (!physical_actual.camera || resolved(physical_frame) != physical_actual.camera->description() || focus(physical_frame) != 3 ||
      make_gaussian_scene_renderer()->render(scene,*physical_actual.camera,true).image != physical_frame.image) return fail("physical breathing/calibration and exact replay");
  const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions options; options.progressive_frames = queue;
  const auto fresh = make_gaussian_scene_renderer(); const auto final = fresh->renderWithOptions(scene,camera,false,{},options);
  const auto prefix = queue->takeLatest(); auto half = manual; half.photo.lens.samples = 16;
  if (!final.error.isEmpty() || !prefix || prefix->metrics.value("camera").toMap() != camera.description() || focus(*prefix) != 3 ||
      resolved(*prefix) != manual.description() || read_gaussian_photo_texture(*prefix) != renderer->render(scene,half,true).image ||
      read_gaussian_photo_texture(final) != first.image) return fail("progressive authored identity and immutable resolved focus");
  auto edited = camera; edited.photo.post.saturation = 1.1F;
  const auto cached = fresh->render(scene,edited,true);
  if (!cached.metrics.value("resolvedSourceReused").toBool() || focus(cached) != 3) return fail("autofocus resolves again while reusing identical optical source");
  auto cancel = std::make_shared<std::atomic_bool>(true);
  const auto canceled = renderer->renderWithOptions(scene,camera,false,{}, {cancel,{},false});
  if (!canceled.canceled || canceled.native_texture) return fail("cancellation publishes no focus frame");
  if (!output.isEmpty()) {
    if (!QDir().mkpath(output) || !first.image.save(QDir(output).filePath("focus-depth-3.png")) ||
        !second.image.save(QDir(output).filePath("focus-depth-2.png")) || !solid.image.save(QDir(output).filePath("solid-centre.png"))) return fail("proof images");
    QFile receipt(QDir(output).filePath("receipt.json")); if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return fail("receipt");
    receipt.write(QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"schema","framehouse-autofocus-proof-v1"},
        {"first",first.metrics},{"moved",second.metrics},{"fallback",fallback.metrics.value("autofocus")},
        {"physical",physical_frame.metrics.value("resolvedCamera")},{"scope","Deterministic centre-ray autofocus and exact manual replay, not lens-motor or realtime qualification"}}).toJson());
  }
  std::cout << "Autofocus: movement/time/geometry, fallback/range, resolution invariance, physical replay, progressive identity, cache and cancellation pass\n";
  return true;
}
