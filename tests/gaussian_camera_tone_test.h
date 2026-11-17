#pragma once
// Known flat surface radiances provide an independent scalar oracle, including
// values above display white. No clipped PNG is used as the HDR reference.
static bool gaussian_camera_tone_regression(const QString& evidence = {}) {
  const auto fail = [](const char* reason) { std::cerr << "Camera tone: " << reason << '\n'; return false; };
  const auto decode = [](double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
  const auto encode = [](double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; };
  using Color = std::array<double,3>;
  const std::array<Color,9> colors{{{0,0,0},{0.18,0.18,0.18},{0.65,0.65,0.65},
      {0.95,0.5,0.2},{0.2,0.8,0.5},{0.05,0.1,0.2},{0.55,0.35,0.9},{0.001,0.001,0.001},{0.025,0.025,0.025}}};
  auto scene = std::make_shared<GaussianSceneData>(); scene->points = {point(-3,{0,0,0})};
  GaussianCamera camera; camera.image_size = {257,257}; camera.fx = camera.fy = 240; camera.cx = camera.cy = 128.5F;
  GaussianSceneComposition composition; composition.ambient = 1;
  std::array<Color,9> radiance{};
  for (size_t i = 0; i < colors.size(); ++i) {
    GaussianSceneObject patch; patch.id = QString::number(i); patch.name = "Tone patch"; patch.kind = 1;
    patch.position = {static_cast<float>(i%3)-1,static_cast<float>(i/3)-1,3}; patch.size = {0.42F,0.42F,0.01F};
    patch.color = {static_cast<float>(encode(colors[i][0])),static_cast<float>(encode(colors[i][1])),static_cast<float>(encode(colors[i][2]))};
    for (size_t c = 0; c < 3; ++c) radiance[i][c] = decode(patch.color[static_cast<int>(c)]);
    composition.objects.push_back(patch);
  }
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  auto renderer = make_gaussian_scene_renderer();
  const auto neutral = renderer->render(scene,camera,true);
  if (!neutral.error.isEmpty()) return fail("neutral chart render");
  int checks = 0, maximum_error = 0;
  for (const float exposure : {-2.0F,0.0F,3.0F}) for (const int look : {0,1,2})
    for (const float contrast : {0.25F,1.0F,2.0F}) for (const float compression : {0.0F,0.5F,1.0F}) {
      camera.photo.exposure_ev = exposure; camera.photo.look = look; camera.photo.tone = {contrast,compression};
      const auto frame = renderer->render(scene,camera,true);
      if (!frame.error.isEmpty()) return fail("tone chart render");
      for (size_t i = 0; i < colors.size(); ++i) {
        Color rgb = radiance[i]; for (auto& c : rgb) c *= std::exp2(exposure);
        const auto luminance = [](const Color& c) { return c[0]*0.2126+c[1]*0.7152+c[2]*0.0722; };
        if (look == 2) { const auto l = luminance(rgb); rgb = {l,l,l}; }
        if (look != 0) {
          const auto peak = *std::max_element(rgb.begin(),rgb.end());
          if (peak > 0.6) for (auto& c : rgb) c *= (0.6+0.4*(-std::expm1(-(peak-0.6)/0.4)))/peak;
        }
        const auto l = luminance(rgb);
        if (contrast != 1 && l > 0) for (auto& c : rgb) c *= (0.18*std::pow(l/0.18,contrast))/l;
        const auto peak = *std::max_element(rgb.begin(),rgb.end());
        if (compression > 0) {
          const double knee = 1-0.8*compression, headroom = 0.8*compression;
          if (peak > knee) for (auto& c : rgb) c *= (knee-headroom*std::expm1(-(peak-knee)/headroom))/peak;
        }
        const int x = static_cast<int>(std::floor(128.5+(static_cast<double>(i%3)-1)*240/2.99));
        const int y = static_cast<int>(std::floor(128.5+(static_cast<double>(i/3)-1)*240/2.99));
        const auto p = frame.image.pixelColor(x,y); const std::array<int,3> actual{p.red(),p.green(),p.blue()};
        for (size_t c = 0; c < 3; ++c) {
          const auto expected = static_cast<int>(std::lround(std::clamp(encode(rgb[c]),0.0,1.0)*255));
          maximum_error = std::max(maximum_error,std::abs(actual[c]-expected));
          if (std::abs(actual[c]-expected) > 1) {
            std::cerr << "Patch " << i << " EV=" << exposure << " look=" << look << " contrast=" << contrast
                << " compression=" << compression << " channel=" << c << " actual=" << actual[c] << " expected=" << expected << '\n';
            return fail("unclipped scalar radiance oracle");
          }
          ++checks;
        }
      }
    }
  camera.photo = {};
  if (renderer->render(scene,camera,true).image != neutral.image) return fail("neutral identity after edits");
  camera.photo.exposure_ev = 3;
  const auto clipped = renderer->render(scene,camera,true);
  camera.photo.tone = {1.2F,0.8F};
  const auto toned = renderer->render(scene,camera,true);
  GaussianRenderOptions live; live.interactive = true;
  if (toned.image == clipped.image || read_gaussian_photo_texture(renderer->render(scene,camera,false)) != toned.image ||
      renderer->renderWithOptions(scene,camera,true,{},live).image != toned.image) return fail("HDR change, texture and interactive parity");
  if (!evidence.isEmpty()) {
    if (QFileInfo::exists(evidence) || !QDir().mkpath(evidence) || !neutral.image.save(evidence+"/neutral.png") ||
        !clipped.image.save(evidence+"/exposed-clipped.png") || !toned.image.save(evidence+"/compressed.png")) return fail("evidence images");
    QFile receipt(evidence+"/receipt.json");
    const QVariantMap result{{"ok",true},{"scalarChannelChecks",checks},{"maximumByteError",maximum_error},
        {"tone",camera.photo.tone.description()},{"model","linear-luminance-contrast-peak-shoulder-v1"},
        {"scope","known linear surface radiances, display transfer and native/live parity; not an Octane or photographic match"}};
    if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument::fromVariant(result).toJson()) < 0) return fail("evidence receipt");
  }
  std::cout << "Camera tone: " << checks << " independent HDR/SDR scalar channel checks; max byte error=" << maximum_error << '\n';
  return true;
}
