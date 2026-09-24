#pragma once
#include <QCryptographicHash>
// Same camera/optics, twelve film/tone edits. Renderer-only timings deliberately
// exclude texture readback, but every output is hashed afterward for parity.
static int gaussian_film_perf(const QStringList& args) {
  if (args.size() != 5) return 1;
  const auto validation = validate_gaussian_ply(args[2], {});
  const auto loaded = load_gaussian_scene(args[2], args[3], validation.value("artifactSha256").toString());
  if (!loaded.scene) return 1;
  auto renderer = make_gaussian_scene_renderer();
  const bool motion = args[1] == "--motion-perf";
  auto camera = loaded.scene->cameras.front();
  auto size = camera.image_size; size.scale(motion ? 720 : 800,motion ? 720 : 800,Qt::KeepAspectRatio); camera = camera.resized(size);
  if (!motion) camera.photo.lens = {loaded.scene->focus_distance, loaded.scene->focus_distance*0.025F,7,0,0,16};
  const auto warm = renderer->render(loaded.scene,camera,false);
  if (!warm.error.isEmpty()) { std::cerr << warm.error.toStdString(); return 1; }
  QVariantList frames;
  std::vector<double> times;
  for (int i = 0; i < (motion ? 30 : 12); ++i) {
    if (!motion) {
      camera.photo.exposure_ev = static_cast<float>(i)*0.125F;
      camera.photo.film = {static_cast<float>(i+1)/12,3,0.4F,8,0.5F,42};
    }
    const auto view = motion ? navigate_gaussian_camera(camera,loaded.scene->focus_distance,static_cast<float>(i+1)*0.15F,0,0,0,0) : camera;
    const auto frame = renderer->renderWithOptions(loaded.scene,view,false,{},{{},{},motion});
    if (!frame.error.isEmpty()) return 1;
    const auto image = read_gaussian_photo_texture(frame);
    if (image.isNull()) return 1;
    auto metrics = frame.metrics;
    const auto bytes = QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes());
    metrics.insert("pixelSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()));
    frames.append(metrics); times.push_back(metrics.value("renderMs").toDouble());
  }
  std::sort(times.begin(),times.end());
  const auto middle = times.size()/2;
  const QVariantMap result{{"schema",motion ? "framehouse-gaussian-motion-perf-v1" : "framehouse-gaussian-film-perf-v1"},
      {"scope",motion ? "native-renderer-motion-not-input-to-photon" : "native-renderer-film-edit-not-input-to-photon"},{"artifactSha256",loaded.scene->artifact_sha256},
      {"camera",camera.description()},{"frames",frames},{"medianMs",(times[middle-1]+times[middle])/2},
      {"p95Ms",times[static_cast<size_t>(std::ceil(static_cast<double>(times.size())*0.95))-1]},{"coldRenderMs",warm.metrics.value("renderMs")}};
  QFile output(args[4]);
  if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly) || output.write(QJsonDocument::fromVariant(result).toJson()) < 0) return 1;
  std::cout << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
