#pragma once
#include <thread>

// Included after point(). Compare every pixel, including tile seams and canvas
// borders, against the existing full-frame path before testing a 24 MP export.
static bool gaussian_tiled_photo_regression() {
  const auto fail = [](const char* message) { std::cerr << "Tiled photo: " << message << '\n'; return false; };
  auto renderer = make_gaussian_scene_renderer();
  auto scene = std::make_shared<GaussianSceneData>();
  GaussianCamera camera; camera.image_size = {257, 193}; camera.fx = 240; camera.fy = 230;
  camera.cx = 131.25F; camera.cy = 91.75F;
  auto background = point(7, {0.4F, 0.7F, 0.9F});
  background.covariance_a = {4, 0.5F, 0, 3}; scene->points.push_back(background);
  for (const float x : {0.5F, 63.5F, 128.0F, 191.5F, 256.5F}) for (const float y : {0.5F, 63.5F, 128.0F, 192.5F}) {
    auto emitter = point(4, {2.5F, 1.8F, 1.2F}); emitter.center_opacity[3] = 0.9F;
    emitter.center_opacity[0] = (x-camera.cx)*4/camera.fx;
    emitter.center_opacity[1] = (y-camera.cy)*4/camera.fy;
    emitter.covariance_a = {0.0004F, 0.0001F, 0, 0.0002F};
    scene->points.push_back(emitter);
  }
  const auto compare = [&](const GaussianFrame& a, const GaussianFrame& b) {
    if (!a.error.isEmpty() || !b.error.isEmpty() || a.image.isNull() || a.image.size() != b.image.size()) {
      std::cerr << a.error.toStdString() << ' ' << b.error.toStdString(); return false;
    }
    int maximum = 0;
    for (int y = 0; y < a.image.height(); ++y) for (int x = 0; x < a.image.width(); ++x) {
      const auto p = a.image.pixelColor(x, y), q = b.image.pixelColor(x, y);
      maximum = std::max({maximum, std::abs(p.red()-q.red()), std::abs(p.green()-q.green()), std::abs(p.blue()-q.blue())});
    }
    if (maximum > 1) std::cerr << "Maximum tile/full difference: " << maximum << '\n';
    return maximum <= 1;
  };
  for (int mode = 0; mode < 5; ++mode) {
    camera.photo = {};
    if (mode == 1 || mode == 4) camera.photo.film = {0.7F, 6, 0, 16, 0.1F, 987};
    if (mode == 2 || mode == 4) { camera.photo.film.halation_amount = 1; camera.photo.film.halation_radius = 16; camera.photo.film.halation_threshold = 0.1F; }
    if (mode >= 3) camera.photo.lens = {3, 0.2F, 7, 27, 0.4F, 16};
    if (mode == 4) { camera.photo.exposure_ev = 0.3F; camera.photo.look = 1; camera.photo.focal_scale = 1.1F; }
    const auto full = renderer->render(scene, camera, true);
    for (const int edge : {64, 97}) {
      const auto tiled = renderer->renderWithOptions(scene, camera, true, {}, {{}, {}, false, edge});
      if (!compare(full, tiled) || tiled.native_texture || tiled.native_lease ||
          !tiled.metrics.value("tiledPhoto").toBool() || tiled.metrics.value("resolvedSourceReused").toBool() ||
          tiled.metrics.value("resolvedSourceBytes").toULongLong() != 0 ||
          tiled.image.colorSpace() != QColorSpace(QColorSpace::SRgb)) return fail("pixel/metadata parity");
      if (renderer->renderWithOptions(scene, camera, true, {}, {{}, {}, false, edge}).image != tiled.image)
        return fail("deterministic repeat");
    }
  }
  // Temporal opacity/position and covariance projection use the full camera too.
  auto moving = std::make_shared<GaussianSceneData>(*scene);
  GaussianTemporalPoint motion;
  motion.velocity_time = {0.2F, 0.1F, 0.05F, 0}; motion.normalization[0] = 1;
  motion.covariance_factor = {0.03F, 0, 0, 0, 0, 0.03F, 0, 0, 0, 0, 0.03F, 0};
  moving->temporal.assign(moving->points.size(), motion); moving->local_time_range = {-1, 1};
  if (!compare(renderer->render(moving, camera, true, 0.3),
      renderer->renderWithOptions(moving, camera, true, 0.3, {{}, {}, false, 64}))) return fail("temporal parity");
  for (const int edge : {-1, 1, 31, 2049})
    if (renderer->renderWithOptions(scene, camera, true, {}, {{}, {}, false, edge}).error.isEmpty()) return fail("invalid tile edge");
  if (renderer->renderWithOptions(scene, camera, false, {}, {{}, {}, false, 64}).error.isEmpty()) return fail("preview readback guard");
  auto large = camera.resized({6000, 4000});
  large.photo = {}; large.photo.lens = {3, 0.1F, 7, 0, 0, 16}; large.photo.film.grain_amount = 0.3F;
  auto simple = std::make_shared<GaussianSceneData>(); simple->points = {background};
  const auto high_resolution = renderer->render(simple, large, true);
  if (!high_resolution.error.isEmpty() || high_resolution.image.size() != QSize(6000, 4000) ||
      high_resolution.metrics.value("tileCount").toInt() != 6 ||
      high_resolution.metrics.value("maximumTilePixels").toInt() > 2048*2048 ||
      high_resolution.metrics.value("apertureSamples").toInt() != 16 ||
      high_resolution.metrics.value("rasterizedApertureSamples").toInt() != 96 ||
      high_resolution.native_texture || high_resolution.image.pixelColor(3000, 2000).blue() == 0) return fail("24 MP optical/grain export");
  const auto canceled = std::make_shared<std::atomic_bool>(false);
  const auto tiles = std::make_shared<std::atomic_int>(0);
  std::jthread canceler([&] {
    QElapsedTimer timer; timer.start();
    while (tiles->load() < 1 && timer.elapsed() < 5000) std::this_thread::yield();
    canceled->store(true);
  });
  const auto interrupted = renderer->renderWithOptions(simple, large, true, {}, {canceled, {}, false, 256, tiles});
  canceler.join();
  if (!interrupted.canceled || !interrupted.image.isNull() || interrupted.native_texture || tiles->load() < 1)
    return fail("mid-photo cancellation publishes no partial image");
  std::cout << "Tiled photos: neutral/film/lens/temporal seam parity, deterministic grain, 24 MP bounded export and cancellation pass\n";
  return true;
}
