#pragma once
#include "gaussian_lens_sampling.h"
#include <QElapsedTimer>
#include <QThread>
#include <thread>

inline bool gaussian_lens_regression(const std::shared_ptr<photo_editor::desktop::GaussianSceneRenderer>& renderer) {
  using namespace photo_editor::desktop;
  const auto fail = [](int stage) { std::cerr << "Thin-lens regression stage " << stage << '\n'; return false; };
  GaussianCamera camera; camera.image_size = {65, 65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.photo.lens = {3, 0.3F, 0, 0, 0, 32};
  const auto project = [](const GaussianCamera& c, QVector3D p) {
    const auto v = c.worldToCamera().map(p);
    return QVector2D(c.fx * c.photo.focal_scale * v.x() / v.z() + c.cx,
        c.fy * c.photo.focal_scale * v.y() / v.z() + c.cy);
  };
  for (const int blades : {0, 3, 6, 7, 9, 16}) for (const float roundness : {0.0F, 0.5F, 1.0F}) {
    auto lens = camera.photo.lens; lens.blades = blades; lens.roundness = roundness; lens.samples = 128;
    const auto samples = gaussian_aperture_samples(lens);
    if (samples.size() != 128 || samples != gaussian_aperture_samples(lens)) return fail(1);
    auto extended = lens; extended.samples = 4096;
    const auto extended_samples = gaussian_aperture_samples(extended);
    if (extended_samples.size() != 4096 || !std::equal(samples.begin(),samples.end(),extended_samples.begin())) return fail(22);
    QVector2D mean;
    double radius2 = 0;
    for (const auto sample : samples) {
      mean += sample; radius2 += sample.lengthSquared();
      if (sample.length() > lens.aperture_radius + 1e-6F) return fail(2);
      for (const float z : {1.5F, 3.0F, 6.0F}) {
        const QVector3D p(0.2F, -0.3F, z);
        const auto actual = project(gaussian_aperture_camera(camera, sample), p) - project(camera, p);
        const QVector2D expected(sample.x() * camera.fx * (1 / lens.focus_distance - 1 / z),
            sample.y() * camera.fy * (1 / lens.focus_distance - 1 / z));
        if ((actual - expected).length() > 1e-4F) return fail(3);
      }
    }
    if ((mean / 128).length() > lens.aperture_radius * 0.08F) return fail(4);
    if ((blades == 0 || roundness == 1) && std::abs(radius2 / 128 / (lens.aperture_radius * lens.aperture_radius) - 0.5) > 0.02) return fail(5);
    lens.rotation_degrees = 90;
    const auto rotated = gaussian_aperture_samples(lens);
    for (size_t i = 0; i < samples.size(); ++i)
      if ((rotated[i] - QVector2D(-samples[i].y(), samples[i].x())).length() > 1e-6F) return fail(6);
  }
  const auto splat = [](float z, float opacity, float color, float variance) {
    GaussianPoint p; p.center_opacity = {0, 0, z, opacity};
    p.covariance_a = {variance, 0, 0, variance}; p.covariance_b = {0, 1e-12F, 0, 0};
    for (size_t channel = 0; channel < 3; ++channel) p.sh[channel] = (color - 0.5F) / 0.2820947918F;
    return p;
  };
  auto plane = std::make_shared<GaussianSceneData>(); plane->points = {splat(3, 0.8F, 0.6F, 0.02F)};
  auto pinhole = camera; pinhole.photo.lens.aperture_radius = 0;
  const auto focused = renderer->render(plane, camera, true);
  const auto sharp = renderer->render(plane, pinhole, true);
  if (!focused.error.isEmpty() || !sharp.error.isEmpty()) { std::cerr << focused.error.toStdString(); return fail(7); }
  for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x)
    if (std::abs(focused.image.pixelColor(x, y).red() - sharp.image.pixelColor(x, y).red()) > 1) return fail(8);
  auto scene = std::make_shared<GaussianSceneData>();
  scene->points = {splat(2, 0.999F, 0, 0.0004F), splat(6, 0.9F, 1, 0.0004F)};
  const auto frame = renderer->render(scene, camera, true);
  if (!frame.error.isEmpty() || frame.metrics.value("apertureSamples").toInt() != 32) return fail(9);
  const auto preview = renderer->render(scene, camera, false);
  if (!preview.image.isNull() || read_gaussian_photo_texture(preview) != frame.image ||
      renderer->render(scene, camera, true).image != frame.image) return fail(10);
  const auto linearize = [](int code) { const double c = code / 255.0; return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
  std::vector<double> reference(65 * 65, 0);
  for (const auto offset : gaussian_aperture_samples(camera.photo.lens)) {
    auto c = gaussian_aperture_camera(camera, offset); c.photo.lens.aperture_radius = 0;
    const auto sample = renderer->render(scene, c, true);
    if (!sample.error.isEmpty()) return fail(11);
    for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x)
      reference[static_cast<size_t>(y * 65 + x)] += linearize(sample.image.pixelColor(x, y).red()) / 32;
  }
  double visible = 0, blocked = 0;
  const auto blocked_frame = renderer->render(scene, pinhole, true);
  for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) {
    const auto value = reference[static_cast<size_t>(y * 65 + x)];
    const auto expected = value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1 / 2.4) - 0.055;
    if (std::abs(frame.image.pixelColor(x, y).red() - std::lround(expected * 255)) > 2) return fail(12);
    visible += linearize(frame.image.pixelColor(x, y).red());
    blocked += linearize(blocked_frame.image.pixelColor(x, y).red());
  }
  if (visible <= blocked * 1.2) return fail(13); // Aperture rays reveal the occluded emitter.
  const auto draft = renderer->renderWithOptions(scene, camera, true, {}, {{}, {}, true});
  if (draft.image != blocked_frame.image || draft.metrics.value("apertureSamples").toInt() != 1) return fail(14);
  // Progressive output is a complete, immutable film-processed prefix, never
  // an alias of the accumulation texture. Final image and cached edits agree.
  auto progressive_scene = std::make_shared<GaussianSceneData>(*scene);
  auto progressive_camera = camera;
  progressive_camera.photo.film = {0.15F, 2, 0.3F, 4, 0.8F, 123};
  const auto queue = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions progressive_options; progressive_options.progressive_frames = queue;
  const auto final = renderer->renderWithOptions(progressive_scene, progressive_camera, false, {}, progressive_options);
  auto partial = queue->takeLatest();
  if (!final.error.isEmpty() || final.metrics.value("progressiveFramesProduced").toInt() != 3 ||
      !partial || queue->takeLatest() || partial->metrics.value("apertureSamples").toInt() != 16 ||
      !partial->image.isNull() || !partial->native_texture || partial->native_texture == final.native_texture) return fail(18);
  const auto prefix_pixels = read_gaussian_photo_texture(*partial);
  const auto full_pixels = read_gaussian_photo_texture(final);
  const auto independent = make_gaussian_scene_renderer();
  if (!independent || independent->render(progressive_scene, progressive_camera, true).image != full_pixels ||
      renderer->render(progressive_scene, progressive_camera, true).image != full_pixels) return fail(19);
  progressive_camera.photo.lens.samples = 16;
  if (renderer->render(progressive_scene, progressive_camera, true).image != prefix_pixels ||
      read_gaussian_photo_texture(*partial) != prefix_pixels) return fail(20);
  const auto readback = renderer->renderWithOptions(progressive_scene, progressive_camera, true, {}, progressive_options);
  const auto interactive = renderer->renderWithOptions(progressive_scene, progressive_camera, false, {},
      {{}, {}, true, 0, {}, queue});
  if (!readback.error.isEmpty() || !interactive.error.isEmpty() || queue->takeLatest()) return fail(21);
  camera.photo.lens.samples = 128;
  const auto canceled = std::make_shared<std::atomic_bool>(false);
  const auto completed = std::make_shared<std::atomic_int>(0);
  std::jthread canceler([&] {
    QElapsedTimer timer; timer.start();
    while (completed->load() < 2 && timer.elapsed() < 2000) std::this_thread::yield();
    canceled->store(true);
  });
  const auto interrupted = renderer->renderWithOptions(scene, camera, false, {}, {canceled, completed, false});
  canceler.join();
  if (!interrupted.canceled || interrupted.native_texture || !interrupted.image.isNull() || completed->load() >= 128) return fail(15);
  completed->store(0);
  if (!renderer->renderWithOptions(scene, camera, true, {}, {canceled, completed, false}).canceled || completed->load() != 0) return fail(16);
  auto oversized = camera; oversized.image_size = {8192, 8192};
  if (!renderer->render(scene, oversized, false).error.contains("4096")) return fail(17);
  std::cout << "Thin lens: aperture geometry, focus plane, per-ray occlusion, exact texture/export parity and cancellation pass\n";
  return true;
}
