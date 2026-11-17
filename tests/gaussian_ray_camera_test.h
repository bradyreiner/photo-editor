#pragma once

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <thread>

// The ray-traced mode is an opt-in primary-ray path. These fixtures use
// isolated, nearly point-like Gaussian emitters so the thin-lens circle of
// confusion can be checked against its independent paraxial prediction.
static bool gaussian_ray_camera_regression(const QString& evidence = {}) {
  const auto fail = [](const char* stage) { std::cerr << "Ray camera: " << stage << '\n'; return false; };
  const auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return fail("Metal renderer unavailable");

  constexpr int edge = 161;
  constexpr float focal_pixels = 160.0F;
  constexpr float pupil_radius = 0.15F;
  GaussianCamera camera;
  camera.image_size = {edge, edge}; camera.fx = camera.fy = focal_pixels;
  camera.cx = camera.cy = static_cast<float>(edge) * 0.5F;
  camera.photo.lens.focus_distance = 1.5F;
  camera.photo.lens.aperture_radius = pupil_radius;
  camera.photo.lens.samples = 128;
  camera.photo.lens.ray_traced = true;

  const auto make_emitter = [&](float z, float center_x) {
    GaussianPoint p;
    p.center_opacity = {(center_x - camera.cx) * z / camera.fx, 0, z, 0.999F};
    const float sigma = z / camera.fx; // one projected pinhole pixel in each axis
    p.covariance_a = {sigma * sigma, 0, 0, sigma * sigma};
    p.covariance_b = {0, 1e-12F, 0, 0};
    for (size_t channel = 0; channel < 3; ++channel) p.sh[channel] = 0.5F / 0.2820947918F;
    return p;
  };
  auto scene = std::make_shared<GaussianSceneData>();
  const std::array<float, 3> depths{1.5F, 3.0F, 5.0F};
  const std::array<float, 3> centers{28.5F, 80.5F, 132.5F};
  for (size_t i = 0; i < depths.size(); ++i) scene->points.push_back(make_emitter(depths[i], centers[i]));

  struct Stats { double energy = 0, variance = 0, mean_x = 0, mean_y = 0; };
  const auto stats = [](const QImage& image, float cx, float cy, int radius) {
    Stats result; double second_x = 0, second_y = 0;
    const auto linear = [](double code) { return code <= 0.04045 ? code / 12.92 : std::pow((code + 0.055) / 1.055, 2.4); };
    for (int y = std::max(0, static_cast<int>(std::floor(cy)) - radius);
         y <= std::min(image.height() - 1, static_cast<int>(std::ceil(cy)) + radius); ++y) {
      for (int x = std::max(0, static_cast<int>(std::floor(cx)) - radius);
           x <= std::min(image.width() - 1, static_cast<int>(std::ceil(cx)) + radius); ++x) {
        const double value = linear(image.pixelColor(x, y).redF());
        result.energy += value;
        result.mean_x += (x + 0.5) * value; result.mean_y += (y + 0.5) * value;
        second_x += (x + 0.5) * (x + 0.5) * value;
        second_y += (y + 0.5) * (y + 0.5) * value;
      }
    }
    if (result.energy > 0) {
      result.mean_x /= result.energy; result.mean_y /= result.energy;
      result.variance = ((second_x / result.energy - result.mean_x * result.mean_x) +
          (second_y / result.energy - result.mean_y * result.mean_y)) * 0.5;
    }
    return result;
  };
  const auto radius_px = [&](float focus, float depth) {
    return static_cast<double>(focal_pixels * pupil_radius) *
        std::abs(1.0 / static_cast<double>(focus) - 1.0 / static_cast<double>(depth));
  };

  auto pinhole_camera = camera;
  pinhole_camera.photo.lens.aperture_radius = 0;
  pinhole_camera.photo.lens.ray_traced = false;
  const auto pinhole = renderer->render(scene, pinhole_camera, true);
  auto focus_near_camera = camera;
  const auto focus_near = renderer->render(scene, focus_near_camera, true);
  auto legacy_optical_camera = focus_near_camera;
  legacy_optical_camera.photo.lens.ray_traced = false;
  const auto legacy_optical = renderer->render(scene, legacy_optical_camera, true);
  auto focus_far_camera = camera; focus_far_camera.photo.lens.focus_distance = depths[2];
  const auto focus_far = renderer->render(scene, focus_far_camera, true);
  for (const auto* frame : {&pinhole, &focus_near, &legacy_optical, &focus_far})
    if (!frame->error.isEmpty()) std::cerr << frame->error.toStdString() << '\n';
  if (!pinhole.error.isEmpty() || !focus_near.error.isEmpty() || !legacy_optical.error.isEmpty() || !focus_far.error.isEmpty() ||
      legacy_optical.metrics.value("resolvedSourceReused").toBool() ||
      legacy_optical.metrics.value("rasterizedApertureSamples").toInt() != camera.photo.lens.samples)
    return fail("focus render failed or lens-model cache did not invalidate");
  const auto pinhole_read = renderer->render(scene, pinhole_camera, false);
  if (!pinhole_read.error.isEmpty() || !pinhole_read.image.isNull() ||
      read_gaussian_photo_texture(pinhole_read) != pinhole.image) return fail("pinhole native texture/readback parity");

  QVariantList coc_measurements;
  for (size_t i = 0; i < depths.size(); ++i) {
    const auto sharp = stats(pinhole.image, centers[i], 80.5F, 28);
    const auto near_result = stats(focus_near.image, centers[i], 80.5F, 28);
    const auto far_result = stats(focus_far.image, centers[i], 80.5F, 28);
    const double near_radius = radius_px(focus_near_camera.photo.lens.focus_distance, depths[i]);
    const double far_radius = radius_px(focus_far_camera.photo.lens.focus_distance, depths[i]);
    const double near_measured = near_result.variance - sharp.variance;
    const double far_measured = far_result.variance - sharp.variance;
    const double near_expected = near_radius * near_radius / 4.0; // variance of a uniform circular pupil projection
    const double far_expected = far_radius * far_radius / 4.0;
    coc_measurements.append(QVariantMap{{"depthSceneUnits", depths[i]}, {"focusNearExpectedCoCRadiusPx", near_radius},
        {"focusNearMeasuredAddedVariancePx2", near_measured}, {"focusFarExpectedCoCRadiusPx", far_radius},
        {"focusFarMeasuredAddedVariancePx2", far_measured}});
    const auto matches = [](double actual, double expected) {
      return std::isfinite(actual) && std::abs(actual - expected) <= std::max(1.0, expected * 0.16);
    };
    if (!matches(near_measured, near_expected) || !matches(far_measured, far_expected)) {
      std::cerr << QJsonDocument::fromVariant(coc_measurements).toJson().constData();
      return fail("measured blur does not match analytic thin-lens CoC");
    }
    if ((i == 0 && !(near_measured < far_measured)) || (i == 2 && !(far_measured < near_measured)))
      return fail("moving focus did not move the sharp plane");
  }

  // Float world positions are already quantized by the scene representation.
  // A 1e5-unit translation keeps the pupil offsets resolvable while stressing
  // the scan hierarchy and camera-relative subtraction at a large origin.
  constexpr float world_shift = 100000.0F;
  auto translated_scene = std::make_shared<GaussianSceneData>(*scene);
  for (auto& p : translated_scene->points) {
    p.center_opacity[0] += world_shift;
    p.center_opacity[1] -= world_shift;
    p.center_opacity[2] += world_shift;
  }
  auto translated_camera = focus_near_camera;
  translated_camera.position = {world_shift, -world_shift, world_shift};
  auto translated_pinhole = pinhole_camera;
  translated_pinhole.position = translated_camera.position;
  const auto translated = renderer->render(translated_scene, translated_camera, true);
  const auto translated_reference = renderer->render(translated_scene, translated_pinhole, true);
  if (!translated.error.isEmpty() || !translated_reference.error.isEmpty()) return fail("large-origin render failed");
  const auto shifted_stats = stats(translated.image, centers[2], 80.5F, 28);
  const auto shifted_reference_stats = stats(translated_reference.image, centers[2], 80.5F, 28);
  const double shifted_variance = shifted_stats.variance - shifted_reference_stats.variance;
  const double shifted_expected = std::pow(radius_px(translated_camera.photo.lens.focus_distance, depths[2]), 2) / 4.0;
  const auto origin_ray_stats = stats(focus_near.image, centers[2], 80.5F, 28);
  if (std::abs(shifted_variance - shifted_expected) > std::max(1.5, shifted_expected * 0.20) ||
      std::abs(shifted_stats.energy / origin_ray_stats.energy - 1) > 0.08) {
    std::cerr << "translated variance " << shifted_variance << " expected " << shifted_expected
        << ", ray energy ratio " << shifted_stats.energy/origin_ray_stats.energy << '\n';
    return fail("large-origin pupil precision exceeded 1e5-unit tolerance");
  }

  // More pupil samples must approach the same smooth disk profile and keep
  // integrated radiance stable. Isolate the far emitter and center its field
  // position so unrelated layers and frame-edge clipping cannot perturb the
  // radiometric comparison.
  auto convergence_scene = std::make_shared<GaussianSceneData>();
  convergence_scene->points = {make_emitter(depths[2],camera.cx)};
  const float convergence_center = camera.cx;
  auto convergence_camera = camera; convergence_camera.photo.lens.samples = 32;
  const auto coarse = renderer->render(convergence_scene, convergence_camera, true);
  convergence_camera.photo.lens.samples = 512;
  const auto fine = renderer->render(convergence_scene, convergence_camera, true);
  if (!coarse.error.isEmpty() || !fine.error.isEmpty()) return fail("sample convergence render failed");
  const auto coarse_stats = stats(coarse.image, convergence_center, 80.5F, 28);
  const auto fine_stats = stats(fine.image, convergence_center, 80.5F, 28);
  const double energy_ratio = fine_stats.energy / coarse_stats.energy;
  const double energy_error_coarse = std::abs(coarse_stats.energy / (2.0 * std::numbers::pi * 0.999) - 1);
  const double energy_error_fine = std::abs(fine_stats.energy / (2.0 * std::numbers::pi * 0.999) - 1);
  if (coarse_stats.energy <= 0 || fine_stats.energy <= 0 || !std::isfinite(energy_ratio) ||
      std::abs(energy_ratio - 1) > 0.06) {
    std::cerr << "apertureEnergy{samples32:" << coarse_stats.energy << ",samples512:" << fine_stats.energy
        << ",ratio512Over32:" << energy_ratio << ",singleGaussianContinuousIntegral:" << 2.0 * std::numbers::pi * 0.999
        << ",relativeError32:" << energy_error_coarse << ",relativeError512:" << energy_error_fine << "}\n";
    return fail("aperture sample count changed integrated energy");
  }
  const auto interior_cv = [&](const QImage& image) {
    const float cx = convergence_center, cy = 80.5F;
    const int interior_radius = static_cast<int>(std::floor(radius_px(camera.photo.lens.focus_distance, depths[2]) * 0.55));
    double sum = 0, squared = 0; int count = 0;
    for (int y = static_cast<int>(cy) - interior_radius; y <= static_cast<int>(cy) + interior_radius; ++y)
      for (int x = static_cast<int>(cx) - interior_radius; x <= static_cast<int>(cx) + interior_radius; ++x)
        if ((x + 0.5F - cx) * (x + 0.5F - cx) + (y + 0.5F - cy) * (y + 0.5F - cy) <= interior_radius * interior_radius) {
          const double code = image.pixelColor(x, y).redF();
          const double value = code <= 0.04045 ? code / 12.92 : std::pow((code + 0.055) / 1.055, 2.4);
          sum += value; squared += value * value; ++count;
        }
    if (!count || sum <= 0) return std::numeric_limits<double>::infinity();
    const double mean = sum / count;
    return std::sqrt(std::max(0.0, squared / count - mean * mean)) / mean;
  };
  const double coarse_cv = interior_cv(coarse.image), fine_cv = interior_cv(fine.image);
  if (!std::isfinite(coarse_cv) || !std::isfinite(fine_cv) || fine_cv > std::max(0.04, coarse_cv * 0.7) || fine_cv > 0.12)
    return fail("smooth bokeh did not converge as sample count increased");
  auto soft_pupil_camera = convergence_camera;
  soft_pupil_camera.photo.lens.pupil_softness = 2;
  const auto soft_pupil = renderer->render(convergence_scene, soft_pupil_camera, true);
  const auto convergence_pinhole_camera = [&] {
    auto result = camera; result.photo.lens.aperture_radius = 0; result.photo.lens.ray_traced = false;
    return result;
  }();
  const auto convergence_pinhole = renderer->render(convergence_scene,convergence_pinhole_camera,true);
  if (!convergence_pinhole.error.isEmpty()) return fail("convergence pinhole reference failed");
  const auto pinhole_far = stats(convergence_pinhole.image, convergence_center, 80.5F, 28);
  const auto soft_pupil_stats = stats(soft_pupil.image, convergence_center, 80.5F, 28);
  const double soft_expected = std::pow(radius_px(camera.photo.lens.focus_distance, depths[2]), 2) / 8.0;
  const double soft_measured = soft_pupil_stats.variance - pinhole_far.variance;
  if (!soft_pupil.error.isEmpty() || std::abs(soft_measured - soft_expected) > std::max(1.5, soft_expected * 0.18) ||
      std::abs(soft_pupil_stats.energy / fine_stats.energy - 1) > 0.08)
    return fail("apodized pupil profile changed its analytic spread or normalized energy");

  // The rendered source is deterministic, exposed texture readback exactly
  // matches export, and switching lens models forces a new resolved source.
  auto high_quality = camera; high_quality.photo.lens.samples = 128;
  const auto ray = renderer->render(scene, high_quality, true);
  const auto ray_preview = renderer->render(scene, high_quality, false);
  const auto ray_repeat = make_gaussian_scene_renderer()->render(scene, high_quality, true);
  if (!ray.error.isEmpty() || !ray_preview.error.isEmpty() || !ray_repeat.error.isEmpty() ||
      ray.image.isNull() || ray.image != ray_repeat.image || ray.image != read_gaussian_photo_texture(ray_preview) ||
      ray.metrics.value("resolvedSourceReused").toBool() ||
      !ray.metrics.value("rayTracedPrimary").toBool() ||
      ray.metrics.value("primaryRaySamplesPerPixel").toInt() != high_quality.photo.lens.samples ||
      ray.metrics.value("primaryRayCount").toLongLong() != static_cast<qint64>(edge) * edge * high_quality.photo.lens.samples ||
      ray.metrics.value("rayTraceBatchSize").toInt() <= 0 ||
      ray.metrics.value("primaryRayTransportModel").toString().isEmpty() ||
      ray.metrics.value("rasterizedApertureSamples").toInt() != 0 ||
      ray.metrics.value("photoSettings").toMap().value("lens").toMap().value("rayTraced").toBool() != true)
    return fail("ray mode determinism, cache invalidation or texture parity");
  const auto cached_ray = renderer->render(scene, high_quality, true);
  if (!cached_ray.metrics.value("resolvedSourceReused").toBool() || cached_ray.image != ray.image)
    return fail("identical ray render failed cache reuse");

  auto cancel_camera = high_quality; cancel_camera.photo.lens.samples = 4096;
  const auto canceled_flag = std::make_shared<std::atomic_bool>(false);
  const auto canceled_samples = std::make_shared<std::atomic_int>(0);
  std::jthread canceler([&] {
    QElapsedTimer wait; wait.start();
    while (canceled_samples->load() < 8 && wait.elapsed() < 10000) std::this_thread::yield();
    canceled_flag->store(true);
  });
  const auto interrupted = make_gaussian_scene_renderer()->renderWithOptions(scene, cancel_camera, false, {},
      {canceled_flag, canceled_samples});
  canceler.join();
  if (!interrupted.canceled || interrupted.native_texture || !interrupted.image.isNull() ||
      canceled_samples->load() < 8 || canceled_samples->load() >= cancel_camera.photo.lens.samples)
    return fail("primary-ray cancellation published a final image or missed its batch boundary");

  // Turning off aperture integration is an exact neutral bypass even when
  // the opt-in ray-tracing switch remains saved in the settings.
  auto zero_ray = high_quality; zero_ray.photo.lens.aperture_radius = 0; zero_ray.photo.lens.samples = 16;
  const auto zero_ray_frame = renderer->render(scene, zero_ray, true);
  auto legacy_pinhole = zero_ray; legacy_pinhole.photo.lens.ray_traced = false;
  const auto legacy_pinhole_frame = renderer->render(scene, legacy_pinhole, true);
  if (!zero_ray_frame.error.isEmpty() || !legacy_pinhole_frame.error.isEmpty() ||
      zero_ray_frame.image != legacy_pinhole_frame.image) return fail("zero-aperture ray mode is not neutral");

  // Post effects run on the ray-resolved source and cannot accidentally reuse
  // stale output pixels. A high threshold is an exact no-op; lowering it must
  // create a visible halo outside the focused emitter.
  auto effect_base_camera = camera; effect_base_camera.photo.lens.focus_distance = depths[2];
  effect_base_camera.photo.lens.samples = 128;
  const auto effect_base = renderer->render(scene, effect_base_camera, true);
  if (!effect_base.error.isEmpty()) return fail("effect baseline render failed");
  auto bloom_camera = effect_base_camera;
  bloom_camera.photo.post.bloom_amount = 2; bloom_camera.photo.post.bloom_radius = 24;
  bloom_camera.photo.post.threshold = 0.1F;
  const auto bloom = renderer->render(scene, bloom_camera, true);
  if (!bloom.error.isEmpty() || bloom.image == effect_base.image ||
      !bloom.metrics.value("resolvedSourceReused").toBool() ||
      bloom.image.pixelColor(132, 80).red() <= effect_base.image.pixelColor(132, 80).red())
    return fail("bloom edit was invisible or reused stale source");
  auto bloom_cutoff_camera = bloom_camera; bloom_cutoff_camera.photo.post.threshold = 16;
  const auto bloom_cutoff = renderer->render(scene, bloom_cutoff_camera, true);
  auto no_post_camera = effect_base_camera; no_post_camera.photo.post = {};
  const auto no_post = renderer->render(scene, no_post_camera, true);
  if (!bloom_cutoff.error.isEmpty() || !no_post.error.isEmpty() || bloom_cutoff.image != no_post.image)
    return fail("high-threshold bloom must exactly bypass");
  auto halo_cutoff_camera = effect_base_camera;
  halo_cutoff_camera.photo.film.halation_amount = 0.9F;
  halo_cutoff_camera.photo.film.halation_radius = 16;
  halo_cutoff_camera.photo.film.halation_threshold = 4;
  const auto halo_cutoff = renderer->render(scene, halo_cutoff_camera, true);
  if (!halo_cutoff.error.isEmpty() || halo_cutoff.image != no_post.image)
    return fail("high-threshold halation must exactly bypass");
  halo_cutoff_camera.photo.film.halation_threshold = 0.1F;
  const auto halo = renderer->render(scene, halo_cutoff_camera, true);
  if (!halo.error.isEmpty() || halo.image == no_post.image || !halo.metrics.value("resolvedSourceReused").toBool() ||
      halo.image.pixelColor(132, 80).red() <= no_post.image.pixelColor(132, 80).red())
    return fail("halation edit was invisible or reused stale source");
  auto halo_bypass_camera = effect_base_camera;
  if (renderer->render(scene, halo_bypass_camera, true).image != no_post.image)
    return fail("disabled halation must exactly bypass");

  const QVariantMap receipt{{"schema", "framehouse-ray-traced-camera-proof-v1"}, {"passed", true},
      {"scope", "synthetic-isolated-Gaussian-sources-and-ideal-thin-lens-CoC-not-photographic-scene-validation"},
      {"circleOfConfusion", coc_measurements}, {"coarseInteriorCv", coarse_cv}, {"fineInteriorCv", fine_cv},
      {"softPupilExpectedAddedVariancePx2", soft_expected}, {"softPupilMeasuredAddedVariancePx2", soft_measured},
      {"translatedWorldOriginUnits", world_shift}, {"translatedAddedVariancePx2", shifted_variance},
      {"translatedExpectedVariancePx2", shifted_expected}, {"1e7WorldOriginQualified", false},
      {"coarseIntegratedEnergy",coarse_stats.energy}, {"fineIntegratedEnergy",fine_stats.energy},
      {"continuousSingleGaussianIntegral",2.0 * std::numbers::pi * 0.999},
      {"coarseIntegratedEnergyRelativeError",energy_error_coarse}, {"fineIntegratedEnergyRelativeError",energy_error_fine},
      {"coarseToFineIntegratedEnergyRatio", energy_ratio},
      {"rayDeterministicReadback", true}, {"rayCacheMissOnModelSwitch", true},
      {"rayCanceledAfterSamples", canceled_samples->load()},
      {"bloomVisibleAndCutoffExact", true}, {"halationVisibleAndCutoffExact", true}};
  if (!evidence.isEmpty()) {
    if (!QDir::isAbsolutePath(evidence) || QFileInfo::exists(evidence) || !QDir().mkpath(evidence) ||
        !pinhole.image.save(QDir(evidence).filePath("pinhole.png")) ||
        !focus_near.image.save(QDir(evidence).filePath("focus-near.png")) ||
        !focus_far.image.save(QDir(evidence).filePath("focus-far.png")) ||
        !coarse.image.save(QDir(evidence).filePath("bokeh-32-samples.png")) ||
        !fine.image.save(QDir(evidence).filePath("bokeh-512-samples.png")) ||
        !soft_pupil.image.save(QDir(evidence).filePath("bokeh-soft-pupil.png")) ||
        !bloom.image.save(QDir(evidence).filePath("bloom.png")) ||
        !halo.image.save(QDir(evidence).filePath("halation.png"))) return fail("proof images");
    QFile file(QDir(evidence).filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return fail("proof receipt");
  }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return true;
}
