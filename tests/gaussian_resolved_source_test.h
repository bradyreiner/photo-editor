#pragma once
#include <functional>
static bool gaussian_resolved_source_regression() {
  auto renderer = make_gaussian_scene_renderer();
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point(3,{1,0.8F,0.4F})};
  GaussianCamera base; base.image_size = {65,65}; base.fx = base.fy = 60; base.cx = base.cy = 32.5F;
  base.photo.lens = {3,0.15F,7,0,0,16};
  const auto warm = renderer->render(scene,base,true);
  if (!warm.error.isEmpty() || warm.metrics.value("resolvedSourceReused").toBool() ||
      warm.metrics.value("depthCenterBytes").toULongLong() != 3*sizeof(float)) return false;
  auto edited = base; edited.photo.exposure_ev = 0.75F; edited.photo.look = 1; edited.photo.film = {0.5F,3,0.4F,8,0.2F,99};
  const auto cached = renderer->render(scene,edited,true);
  const auto independent = make_gaussian_scene_renderer()->render(scene,edited,true);
  if (!cached.error.isEmpty() || cached.image != independent.image || !cached.metrics.value("resolvedSourceReused").toBool() ||
      cached.metrics.value("rasterizedApertureSamples").toInt() != 0 || cached.metrics.value("resolvedSourceBytes").toULongLong() != 65*65*16) return false;
  const std::vector<std::function<void(GaussianCamera&)>> changes{
      [](auto& c) { c.position.setX(0.1F); }, [](auto& c) { c.fx += 1; }, [](auto& c) { c.fy += 1; },
      [](auto& c) { c.cx += 1; }, [](auto& c) { c.cy += 1; }, [](auto& c) { c.photo.focal_scale = 1.2F; },
      [](auto& c) { c.photo.lens.focus_distance = 4; }, [](auto& c) { c.photo.lens.aperture_radius = 0.1F; },
      [](auto& c) { c.photo.lens.blades = 9; }, [](auto& c) { c.photo.lens.rotation_degrees = 30; },
      [](auto& c) { c.photo.lens.roundness = 0.5F; }, [](auto& c) { c.photo.lens.samples = 32; },
      [](auto& c) { c = c.resized({64,64}); },
      [](auto& c) { c = navigate_gaussian_camera(c,3,5,2,0,0,0); }};
  for (const auto& change : changes) {
    auto camera = edited; change(camera);
    const auto miss = renderer->render(scene,camera,true);
    const auto hit = renderer->render(scene,camera,true);
    if (!miss.error.isEmpty() || miss.metrics.value("resolvedSourceReused").toBool() ||
        !hit.metrics.value("resolvedSourceReused").toBool() || hit.image != miss.image) return false;
  }
  const auto before_time = renderer->render(scene,edited,true,0.0);
  const auto changed_time = renderer->render(scene,edited,true,1.0);
  const auto same_time = renderer->render(scene,edited,true,1.0);
  if (!before_time.error.isEmpty() || changed_time.metrics.value("resolvedSourceReused").toBool() ||
      !same_time.metrics.value("resolvedSourceReused").toBool()) return false;
  auto other = std::make_shared<GaussianSceneData>(*scene);
  other->points[0].center_opacity[2] = 5;
  other->points.push_back(point(2,{0.1F,0.3F,0.9F}));
  const auto changed_scene = renderer->render(other,edited,true,1.0);
  if (changed_scene.metrics.value("resolvedSourceReused").toBool() ||
      changed_scene.metrics.value("depthCenterBytes").toULongLong() != 6*sizeof(float) ||
      changed_scene.image != make_gaussian_scene_renderer()->render(other,edited,true,1.0).image) return false;
  const auto canceled = std::make_shared<std::atomic_bool>(true);
  if (!renderer->renderWithOptions(scene,edited,true,1.0,{canceled,{},false}).canceled ||
      renderer->render(other,edited,true,1.0).image != changed_scene.image) return false;
  const auto draft = renderer->renderWithOptions(other,edited,true,1.0,{{},{},true});
  if (draft.metrics.value("resolvedSourceReused").toBool() || draft.metrics.value("filmEffectsApplied").toBool()) return false;
  // A pinhole draft must not replace the retained resolved optical source.
  if (!renderer->render(other,edited,true,1.0).metrics.value("resolvedSourceReused").toBool()) return false;
  std::cout << "Resolved source: exact independent film parity, pose/intrinsics/lens/size/time/scene invalidation and draft isolation pass\n";
  return true;
}
