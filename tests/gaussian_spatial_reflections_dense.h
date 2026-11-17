#pragma once

// Explicit full-resolution A/B diagnostic. Kept separate from synthetic CTest
// oracles; the recipe and scene are frozen before rendering either mode.
int gaussian_spatial_reflections_dense(const QStringList& args) {
  const auto option = [&](const QString& name) { const auto i = args.indexOf(name); return i < 0 ? QString{} : args.value(i+1); };
  const auto project = option("--dense-project"), recipe = option("--recipe"), output = option("--proof-dir");
  const bool diffuse = args.contains("--diffuse");
  const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
  if (!QFileInfo(output).isAbsolute() || QFileInfo::exists(output) || !QDir().mkpath(output)) return 2;
  const auto source = QJsonDocument::fromJson(read(recipe)).toVariant().toMap();
  const auto composition_data = source.contains("acceptedComposition") ? source.value("acceptedComposition").toMap() : source.value("sceneComposition").toMap();
  const auto parsed = gaussian_scene_composition(composition_data); if (!parsed || !(diffuse ? parsed->spatial_environment : parsed->spatial_reflections)) return 2;
  const auto manifest = QJsonDocument::fromJson(read(project+"/scene.json")).toVariant().toMap();
  const auto loaded = load_gaussian_scene(project+"/trained/scene.ply",project+"/dataset/sparse",manifest.value("artifactValidation").toMap().value("artifactSha256").toString());
  if (!loaded.scene || !loaded.scene->temporal.empty()) return 2;
  const auto camera_data = source.value("metrics").toMap().value("camera").toMap();
  if (camera_data.isEmpty() || camera_data.contains("photo")) return 2;
  GaussianCamera camera;
  const auto xyz = [&](const char* name) { const auto v = camera_data.value(name).toList(); return QVector3D(v.value(0).toFloat(),v.value(1).toFloat(),v.value(2).toFloat()); };
  camera.position = xyz("position"); camera.right = xyz("right"); camera.down = xyz("down"); camera.forward = xyz("forward");
  camera.fx = camera_data.value("fx").toFloat(); camera.fy = camera_data.value("fy").toFloat();
  camera.cx = camera_data.value("cx").toFloat(); camera.cy = camera_data.value("cy").toFloat();
  camera.image_size = {camera_data.value("width").toInt(),camera_data.value("height").toInt()};
  auto composition = *parsed; auto renderer = make_gaussian_scene_renderer(); QVariantList frames;
  const auto base = camera;
  for (int i = 0; i < 3; ++i) {
    if (diffuse) composition.spatial_environment = i != 0;
    else composition.spatial_reflections = i != 0;
    camera = i == 2 ? navigate_gaussian_camera(base,loaded.scene->focus_distance,0.01F,0,0,0,0) : base;
    camera.composition = std::make_shared<GaussianSceneComposition>(composition);
    const auto frame = renderer->render(loaded.scene,camera,true);
    const auto name = i == 0 ? QStringLiteral("probe.png") : i == 1 ? QStringLiteral("spatial.png") : QStringLiteral("spatial-warm.png");
    if (!frame.error.isEmpty() || frame.image.isNull() || !frame.image.save(output+"/"+name)) { std::cerr << frame.error.toStdString() << '\n'; return 1; }
    frames.append(QVariantMap{{"file",name},{"metrics",frame.metrics}});
    const QVariantMap result{{"schema","looksy-spatial-reflections-dense-v1"},{"kind",diffuse ? "diffuse" : "reflections"},{"passed",i == 2},{"artifactSha256",loaded.scene->artifact_sha256},
        {"camera",base.description()},{"composition",parsed->description()},{"frames",frames},
        {"scope","full-resolution frozen probe/spatial A/B and warm pose; not sustained interaction or photographic parity"}};
    QFile f(output+"/receipt.json"); const auto bytes = QJsonDocument::fromVariant(result).toJson();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) return 2;
    std::cerr << name.toStdString() << " renderMs=" << frame.metrics.value("renderMs").toDouble() << '\n';
  }
  return 0;
}
