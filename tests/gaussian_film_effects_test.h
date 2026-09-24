#pragma once
// Included after the synthetic point helper in gaussian_scene_render_test.cpp.
static bool gaussian_film_effects_regression(const std::shared_ptr<GaussianSceneRenderer>& renderer) {
  const auto check = [](bool ok, const char* stage) {
    if (!ok) std::cerr << "Film effects regression: " << stage << '\n';
    return ok;
  };
  GaussianCamera camera; camera.image_size = {257, 257}; camera.fx = camera.fy = 240; camera.cx = camera.cy = 128.5F;
  auto scene = std::make_shared<GaussianSceneData>();
  auto emitter = point(3, {1.8F, 1.8F, 1.8F});
  emitter.covariance_a = {0.0005F, 0, 0, 0.0005F};
  scene->points = {emitter};
  const auto base = renderer->render(scene, camera, true);
  if (!check(base.error.isEmpty() && !base.image.isNull(), "base render")) return false;
  camera.photo.film.halation_amount = 1;
  camera.photo.film.halation_radius = 16;
  camera.photo.film.halation_threshold = 0.1F;
  const auto halo = renderer->render(scene, camera, true);
  if (!check(halo.error.isEmpty() && halo.image != base.image, "halation must affect highlights")) return false;
  // Independent double-precision separable convolution of the quantized neutral
  // image, with zero extension. Tolerance includes its 8-bit source quantization.
  const auto decode = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c+0.055)/1.055, 2.4); };
  const auto encode = [](double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c, 1/2.4)-0.055; };
  const double sigma = 16.0 * 257 / 2048;
  const int radius = static_cast<int>(std::ceil(3*sigma));
  std::vector<double> weights, horizontal(257*257);
  double norm = 0;
  for (int i = -radius; i <= radius; ++i) { const double w = std::exp(-0.5*i*i/(sigma*sigma)); weights.push_back(w); norm += w; }
  for (auto& w : weights) w /= norm;
  const auto index = [](int x, int y) { return static_cast<size_t>(y*257+x); };
  for (int y = 0; y < 257; ++y) for (int x = 0; x < 257; ++x)
    for (int i = -radius; i <= radius; ++i) if (x+i >= 0 && x+i < 257)
      horizontal[index(x,y)] += weights[static_cast<size_t>(i+radius)] *
          std::max(0.0, decode(base.image.pixelColor(x+i,y).red()/255.0)-0.1);
  int halo_pixels = 0;
  for (int y = 0; y < 257; ++y) for (int x = 0; x < 257; ++x) {
    double blur = 0;
    for (int i = -radius; i <= radius; ++i) if (y+i >= 0 && y+i < 257)
      blur += weights[static_cast<size_t>(i+radius)] * horizontal[index(x,y+i)];
    const double input = decode(base.image.pixelColor(x,y).red()/255.0);
    const auto pixel = halo.image.pixelColor(x,y);
    const std::array<int,3> actual{pixel.red(), pixel.green(), pixel.blue()};
    const std::array<double,3> tint{1,0.15,0.025};
    for (size_t c = 0; c < 3; ++c)
      if (!check(std::abs(actual[c] - std::lround(std::clamp(encode(input+blur*tint[c]),0.0,1.0)*255)) <= 2,
          "independent halation reference")) return false;
    if (base.image.pixelColor(x,y).red() == 0 && pixel.red() > pixel.green() && pixel.green() >= pixel.blue()) ++halo_pixels;
  }
  if (!check(halo_pixels > 10, "warm light must spread outside emitter")) return false;
  camera.photo.film.halation_threshold = 4;
  if (!check(renderer->render(scene,camera,true).image == base.image, "below-threshold bypass")) return false;
  camera.photo.film.halation_threshold = 0.1F;
  camera.photo.look = 2;
  const auto mono = renderer->render(scene,camera,true);
  for (int y = 0; y < 257; ++y) for (int x = 0; x < 257; ++x) {
    const auto p = mono.image.pixelColor(x,y);
    if (!check(p.red() == p.green() && p.green() == p.blue(), "monochrome after halation")) return false;
  }
  auto flat = std::make_shared<GaussianSceneData>();
  auto gray = point(3, {1,1,1}); gray.covariance_a = {10000,0,0,10000}; flat->points = {gray};
  camera.photo = {};
  const auto clean = renderer->render(flat,camera,true);
  camera.photo.film = {1,6,0,4,0.8F,123456};
  const auto grain = renderer->render(flat,camera,true);
  const auto preview = renderer->render(flat,camera,false);
  if (!check(grain.error.isEmpty() && preview.error.isEmpty() && grain.image != clean.image &&
      grain.image == read_gaussian_photo_texture(preview) && renderer->render(flat,camera,true).image == grain.image,
      "deterministic grain and native preview parity")) return false;
  double mean = 0, square = 0;
  for (int y = 0; y < 257; ++y) for (int x = 0; x < 257; ++x) {
    const auto p = grain.image.pixelColor(x,y);
    const double d = p.red()-clean.image.pixelColor(x,y).red(); mean += d; square += d*d;
    if (!check(p.red() == p.green() && p.green() == p.blue(), "neutral grain chroma")) return false;
  }
  mean /= 257*257; square /= 257*257;
  if (!check(std::abs(mean) < 0.8 && square > 2, "grain mean and variance")) return false;
  ++camera.photo.film.seed;
  if (!check(renderer->render(flat,camera,true).image != grain.image, "seed variation")) return false;
  GaussianRenderOptions interactive; interactive.interactive = true;
  if (!check(renderer->renderWithOptions(flat,camera,true,{},interactive).image == clean.image, "interactive draft omits film")) return false;
  camera.photo.film = {0,6,0,16,0.1F,999};
  if (!check(renderer->render(flat,camera,true).image == clean.image, "zero amounts exact bypass")) return false;
  // All-black stays black even with both effects at maximum.
  auto black = std::make_shared<GaussianSceneData>(); black->points = {point(-1,{1,1,1})};
  camera.photo.film = {1,6,1,16,0.1F,0};
  const auto darkness = renderer->render(black,camera,true);
  if (!check(darkness.error.isEmpty(), "black render")) return false;
  for (int y = 0; y < 257; ++y) for (int x = 0; x < 257; ++x)
    if (!check(darkness.image.pixelColor(x,y) == QColor(Qt::black), "no halo or grain light from black")) return false;
  camera.image_size = {8192,8192};
  if (!check(renderer->render(flat,camera,false).error.contains("4096"), "film output bound")) return false;
  std::cout << "Film: independent halo convolution, warm spread, threshold, monochrome, grain variance/seed, native parity and draft/bypass checks pass\n";
  return true;
}
