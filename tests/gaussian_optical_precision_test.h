#pragma once

// Translation must not turn a focused thin surface into a blur. Use exactly
// representable point/camera translations so this isolates pupil integration,
// not lost information in the float scene coordinates themselves.
inline bool gaussian_optical_precision_regression(const QString& output = {}) {
  using namespace photo_editor::desktop;
  if (!output.isEmpty() && (!QDir::isAbsolutePath(output) || QDir(output).exists() || !QDir().mkpath(output))) return false;
  const auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return false;
  QVariantList cases; bool passed = true;
  QImage origin_sh, origin_temporal;
  for (const float translation : {0.0F,1000000.0F,10000000.0F}) {
    GaussianCamera camera; camera.image_size = {129,129};
    camera.fx = camera.fy = 200; camera.cx = camera.cy = 64.5F;
    camera.position = {translation,0,0};
    camera.photo.lens = {3,0.3F,0,0,0,128};
    auto scene = std::make_shared<GaussianSceneData>();
    GaussianPoint point; point.center_opacity = {translation,0,3,0.999F};
    point.covariance_a = {0.000225F,0,0,0.000225F}; point.covariance_b = {0,1e-12F,0,0};
    for (size_t channel = 0; channel < 3; ++channel) point.sh[channel] = 0.5F/0.2820947918F;
    scene->points = {point};
    auto pinhole_camera = camera; pinhole_camera.photo.lens.aperture_radius = 0;
    const auto pinhole = renderer->render(scene,pinhole_camera,true);
    const auto focused = renderer->render(scene,camera,true);
    if (!pinhole.error.isEmpty() || !focused.error.isEmpty()) return false;
    int max_error = 0; double squared_error = 0;
    for (int y = 0; y < 129; ++y) for (int x = 0; x < 129; ++x) {
      const int delta = focused.image.pixelColor(x,y).red()-pinhole.image.pixelColor(x,y).red();
      max_error = std::max(max_error,std::abs(delta)); squared_error += delta*delta;
    }
    passed = passed && max_error <= 1;
    auto appearance_scene = std::make_shared<GaussianSceneData>(*scene); appearance_scene->sh_degree = 3;
    for (size_t i = 0; i < 48; ++i) appearance_scene->points[0].sh[i] = static_cast<float>(static_cast<int>(i % 7)-3)*0.07F;
    const auto appearance = renderer->render(appearance_scene,camera,true);
    auto temporal_scene = std::make_shared<GaussianSceneData>(*appearance_scene);
    GaussianTemporalPoint motion; motion.velocity_time = {0,0,0,0.5F}; motion.normalization[0] = 1;
    motion.covariance_factor = {0.015F,0,0,0, 0,0.015F,0,0, 0,0,1e-6F,0};
    temporal_scene->temporal = {motion};
    const auto temporal = renderer->render(temporal_scene,camera,true,0.5);
    if (!appearance.error.isEmpty() || !temporal.error.isEmpty()) return false;
    if (translation == 0) { origin_sh = appearance.image; origin_temporal = temporal.image; }
    const bool appearance_equal = appearance.image == origin_sh;
    const bool temporal_equal = temporal.image == origin_temporal;
    passed = passed && appearance_equal && temporal_equal;
    cases.append(QVariantMap{{"worldTranslation",translation},{"maxFocusedPinholeCodeError",max_error},
        {"spatialShTranslationPixelsExact",appearance_equal},{"temporalTranslationPixelsExact",temporal_equal},
        {"focusedCenterCode",focused.image.pixelColor(64,64).red()},{"pinholeCenterCode",pinhole.image.pixelColor(64,64).red()},
        {"meanSquaredCodeError",squared_error/(129*129)}});
    if (!output.isEmpty()) {
      const auto prefix = QDir(output).filePath(QString::number(static_cast<int>(translation)));
      if (!pinhole.image.save(prefix+"-pinhole.png") || !focused.image.save(prefix+"-focused.png")) return false;
    }
  }
  const QVariantMap receipt{{"schema","framehouse-optical-translation-proof-v1"},{"passed",passed},{"cases",cases},
      {"scope","representable-static-focused-surface-not-arbitrary-coordinate-precision"}};
  if (!output.isEmpty()) {
    QFile file(QDir(output).filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(QJsonDocument::fromVariant(receipt).toJson()) < 0) return false;
  }
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact).constData() << '\n';
  return passed;
}
