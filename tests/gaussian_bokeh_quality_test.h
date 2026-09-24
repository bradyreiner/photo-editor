#pragma once
#include <thread>

// A constant-radiance point convolved with a uniform circular pupil has an
// essentially flat interior, away from its blurred boundary. Measure residual
// sampling structure there in linear light, without grain or a denoising pass.
inline bool gaussian_bokeh_quality_regression(const QString& output = {}) {
  using namespace photo_editor::desktop;
  const auto fail = [](const char* message) { std::cerr << "Bokeh quality: " << message << '\n'; return false; };
  if (!output.isEmpty() && (!QDir::isAbsolutePath(output) || QDir(output).exists() || !QDir().mkpath(output))) return false;
  const auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return false;
  GaussianCamera camera; camera.image_size = {129,129};
  camera.fx = camera.fy = 200; camera.cx = camera.cy = 64.5F;
  camera.photo.exposure_ev = 4;
  camera.photo.lens = {3,0.3F,0,0,0,128};
  auto scene = std::make_shared<GaussianSceneData>();
  GaussianPoint point; point.center_opacity = {0,0,1.5F,0.999F};
  const float variance = (1.5F/200)*(1.5F/200);
  point.covariance_a = {variance,0,0,variance}; point.covariance_b = {0,1e-12F,0,0};
  for (size_t channel = 0; channel < 3; ++channel) point.sh[channel] = 0.5F/0.2820947918F;
  scene->points = {point};
  const auto statistics = [](const QImage& image) {
    double sum = 0, squared = 0, total = 0; int count = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const double code = image.pixelColor(x,y).redF();
      const double linear = code <= 0.04045 ? code/12.92 : std::pow((code+0.055)/1.055,2.4);
      total += linear;
      if ((x-64)*(x-64)+(y-64)*(y-64) <= 100) { sum += linear; squared += linear*linear; ++count; }
    }
    const double mean = sum/count;
    return QVariantMap{{"interiorMeanLinear",mean},{"interiorCoefficientOfVariation",std::sqrt(std::max(0.0,squared/count-mean*mean))/mean},
        {"totalLinearEnergy",total}};
  };
  QVariantList levels; QImage final_pixels; double baseline_noise = 0, baseline_energy = 0;
  for (const int samples : {128,256,512,1024,2048,4096}) {
    camera.photo.lens.samples = samples;
    const auto frame = renderer->render(scene,camera,true);
    if (!frame.error.isEmpty() || frame.image.isNull()) return fail("render failed");
    auto metrics = statistics(frame.image); metrics.insert("samples",samples);
    const double noise = metrics.value("interiorCoefficientOfVariation").toDouble();
    const double energy = metrics.value("totalLinearEnergy").toDouble();
    if (!std::isfinite(noise) || !std::isfinite(energy) || energy <= 0) return fail("invalid measurement");
    if (samples == 128) { baseline_noise = noise; baseline_energy = energy; }
    if (std::abs(energy/baseline_energy-1) > 0.02) return fail("sampling changed integrated brightness");
    if (samples == 4096 && (noise > baseline_noise*0.35 || noise > 0.08)) {
      std::cerr << QJsonDocument::fromVariant(metrics).toJson().constData(); return fail("insufficient smoothing improvement");
    }
    levels.append(metrics); final_pixels = frame.image;
    if (!output.isEmpty() && !frame.image.save(QDir(output).filePath(QString("bokeh-%1.png").arg(samples)))) return false;
  }
  const auto queue = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions options; options.progressive_frames = queue;
  // A fresh renderer avoids the final-image cache and exercises all prefixes.
  const auto fresh = make_gaussian_scene_renderer();
  const auto final = fresh->renderWithOptions(scene,camera,false,{},options);
  auto prefix = queue->takeLatest();
  if (!final.error.isEmpty() || read_gaussian_photo_texture(final) != final_pixels ||
      final.metrics.value("progressiveFramesProduced").toInt() != 10 || !prefix ||
      prefix->metrics.value("apertureSamples").toInt() != 2048) return fail("high-quality progressive final mismatch");
  const auto prefix_pixels = read_gaussian_photo_texture(*prefix);
  auto half_camera = camera; half_camera.photo.lens.samples = 2048;
  if (renderer->render(scene,half_camera,true).image != prefix_pixels || read_gaussian_photo_texture(*prefix) != prefix_pixels)
    return fail("progressive prefix is not exact and immutable");
  // Grain/halation remain outside optical integration: replay is deterministic
  // and reuse must not rerasterize 4096 aperture views for a film-only edit.
  camera.photo.film = {0.2F,2,0.3F,4,0.8F,42};
  const auto film = fresh->render(scene,camera,false);
  if (!film.error.isEmpty() || !film.metrics.value("resolvedSourceReused").toBool() ||
      film.metrics.value("rasterizedApertureSamples").toInt() != 0 ||
      fresh->render(scene,camera,true).image != read_gaussian_photo_texture(film)) return fail("film replay/cache mismatch");
  const auto canceled = std::make_shared<std::atomic_bool>(false);
  const auto completed = std::make_shared<std::atomic_int>(0);
  std::jthread canceler([&] {
    QElapsedTimer timer; timer.start();
    while (completed->load() < 8 && timer.elapsed() < 5000) std::this_thread::yield();
    canceled->store(true);
  });
  const auto interrupted = renderer->renderWithOptions(scene,camera,false,{}, {canceled,completed,false});
  canceler.join();
  if (!interrupted.canceled || interrupted.native_texture || !interrupted.image.isNull() || completed->load() >= 4096)
    return fail("high-quality cancellation published a final image");
  const QVariantMap receipt{{"schema","framehouse-bokeh-sampling-proof-v1"},{"passed",true},
      {"scope","synthetic-circular-pupil-interior-convergence-not-measured-lens"},{"levels",levels},
      {"progressiveFramesProduced",10},{"finalTextureMatchesReadback",true},{"filmOnlySourceReuse",true},
      {"canceledAfterSamples",completed->load()}};
  if (!output.isEmpty()) {
    QFile file(QDir(output).filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return false;
  }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return true;
}
