#pragma once
// Independent image-space oracles use the neutral 8-bit render as input. Its
// quantization is included in the tolerance; HDR is generated with +2 EV BEFORE
// filtering, which must never use the clipped exposed PNG as its input.
static bool gaussian_camera_post_regression(const QString& evidence = {}) {
  const auto fail = [](const char* stage) { std::cerr << "Camera post: " << stage << '\n'; return false; };
  const auto decode = [](double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
  const auto encode = [](double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; };
  using Color = std::array<double,3>;
  constexpr int n = 257;
  GaussianCamera camera; camera.image_size = {n,n}; camera.fx = camera.fy = 240; camera.cx = camera.cy = 128.5F;
  auto scene = std::make_shared<GaussianSceneData>();
  auto emitter = point(3,{1.8F,1.2F,0.6F}); emitter.covariance_a = {0.0005F,0,0,0.0005F}; scene->points = {emitter};
  auto renderer = make_gaussian_scene_renderer();
  const auto base = renderer->render(scene,camera,true);
  if (!base.error.isEmpty()) return fail("neutral render");
  std::vector<Color> input(n*n), highlights(n*n), horizontal(n*n);
  const auto index = [](int x,int y) { return static_cast<size_t>(y*n+x); };
  const auto outside = [&](const std::vector<Color>& image,int x,int y) -> Color {
    return x >= 0 && y >= 0 && x < n && y < n ? image[index(x,y)] : Color{};
  };
  const double threshold = 0.5;
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
    const auto p = base.image.pixelColor(x,y); Color rgb{decode(p.redF())*4,decode(p.greenF())*4,decode(p.blueF())*4};
    input[index(x,y)] = rgb;
    const double l = rgb[0]*0.2126+rgb[1]*0.7152+rgb[2]*0.0722;
    for (auto& c : rgb) c *= std::max(0.0,l-threshold)/std::max(l,1e-10);
    highlights[index(x,y)] = rgb;
  }
  const auto matches = [&](const QImage& image,const std::vector<Color>& reference) {
    if (image.size() != QSize(n,n)) return false;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
      const auto p = image.pixelColor(x,y); const std::array<int,3> actual{p.red(),p.green(),p.blue()};
      for (size_t c = 0; c < 3; ++c) {
        const auto expected = std::lround(std::clamp(encode(std::max(0.0,reference[index(x,y)][c])),0.0,1.0)*255);
        if (std::abs(actual[c]-expected) > 2) {
          std::cerr << "At " << x << ',' << y << " channel " << c << ": " << actual[c] << " vs " << expected << '\n'; return false;
        }
      }
    }
    return true;
  };
  camera.photo.exposure_ev = 2; camera.photo.post.bloom_amount = 2; camera.photo.post.bloom_radius = 32; camera.photo.post.threshold = threshold;
  const auto bloom = renderer->render(scene,camera,true);
  const double sigma = 32.0*n/2048; const int radius = static_cast<int>(std::ceil(3*sigma));
  std::vector<double> weights; double norm = 0;
  for (int k = -radius; k <= radius; ++k) { const double w = std::exp(-0.5*k*k/(sigma*sigma)); weights.push_back(w); norm += w; }
  for (auto& w : weights) w /= norm;
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) for (int k = -radius; k <= radius; ++k) {
    const auto v = outside(highlights,x+k,y);
    for (size_t c = 0; c < 3; ++c) horizontal[index(x,y)][c] += weights[static_cast<size_t>(k+radius)]*v[c];
  }
  auto reference = input;
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) for (int k = -radius; k <= radius; ++k) {
    const auto v = outside(horizontal,x,y+k);
    for (size_t c = 0; c < 3; ++c) reference[index(x,y)][c] += 2*weights[static_cast<size_t>(k+radius)]*v[c];
  }
  if (!bloom.error.isEmpty() || !matches(bloom.image,reference)) return fail("independent HDR RGB bloom convolution");
  camera.photo.post.bloom_amount = 0; camera.photo.post.glare_amount = 3; camera.photo.post.glare_radius = 64; camera.photo.post.glare_rays = 1;
  const double length = 64.0*n/2048; const int streak_radius = static_cast<int>(std::ceil(length));
  GaussianFrame glare;
  for (const float spectral : {0.0F,1.0F}) {
    camera.photo.post.spectral_intensity = spectral;
    glare = renderer->render(scene,camera,true); reference = input;
    std::vector<Color> spectral_weights; Color spectral_norm{};
    for (int k = -streak_radius; k <= streak_radius; ++k) {
      const double t = std::abs(k)/length; Color w{};
      for (size_t c = 0; c < 3; ++c) {
        const double wave = 0.5+0.5*std::cos(2*M_PI*(t*1.5-static_cast<double>(c)/3));
        w[c] = std::exp(-3*t)*(1-spectral+spectral*(0.1+wave*wave)); spectral_norm[c] += w[c];
      }
      spectral_weights.push_back(w);
    }
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) for (int k = -streak_radius; k <= streak_radius; ++k) {
      const auto v = outside(highlights,x+k,y);
      for (size_t c = 0; c < 3; ++c) reference[index(x,y)][c] += 3*spectral_weights[static_cast<size_t>(k+streak_radius)][c]/spectral_norm[c]*v[c];
    }
    if (!glare.error.isEmpty() || !matches(glare.image,reference)) return fail("independent spectral streak convolution");
  }
  const auto unrotated = glare.image;
  camera.photo.post.glare_rotation = 45;
  if (renderer->render(scene,camera,true).image == unrotated) return fail("glare rotation");
  camera.photo.post.glare_rays = 3;
  const auto star = renderer->render(scene,camera,true);
  if (star.image == unrotated || read_gaussian_photo_texture(renderer->render(scene,camera,false)) != star.image) return fail("ray count and native texture parity");
  camera.photo.post.threshold = 16;
  const auto cutoff = renderer->render(scene,camera,true);
  camera.photo.post = {};
  if (cutoff.image != renderer->render(scene,camera,true).image) return fail("highlight cutoff bypass");
  // Off-axis achromatic edge: red and blue must move oppositely around the full
  // image center, with bilinear interpolation in linear light and green fixed.
  emitter = point(3,{1.6F,1.6F,1.6F}); emitter.center_opacity[0] = 1; emitter.center_opacity[1] = 0.6F;
  emitter.covariance_a = {0.004F,0,0,0.004F}; scene = std::make_shared<GaussianSceneData>(); scene->points = {emitter};
  camera.photo = {}; const auto plain = renderer->render(scene,camera,true);
  std::vector<Color> gray(n*n);
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
    const double value = decode(plain.image.pixelColor(x,y).redF()); gray[index(x,y)].fill(value);
  }
  const auto bilinear = [&](double x,double y) {
    x = std::clamp(x,0.0,n-1.0); y = std::clamp(y,0.0,n-1.0);
    const int ix = static_cast<int>(x), iy = static_cast<int>(y); const double fx = x-ix, fy = y-iy;
    return gray[index(ix,iy)][0]*(1-fx)*(1-fy)+gray[index(std::min(ix+1,n-1),iy)][0]*fx*(1-fy)+
        gray[index(ix,std::min(iy+1,n-1))][0]*(1-fx)*fy+gray[index(std::min(ix+1,n-1),std::min(iy+1,n-1))][0]*fx*fy;
  };
  reference = gray;
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
    const double dx = (x+0.5-n*0.5)*8/1024, dy = (y+0.5-n*0.5)*8/1024;
    reference[index(x,y)][0] = bilinear(x+dx,y+dy); reference[index(x,y)][2] = bilinear(x-dx,y-dy);
  }
  camera.photo.post.chromatic_aberration = 8;
  const auto chromatic = renderer->render(scene,camera,true);
  if (chromatic.image == plain.image || !matches(chromatic.image,reference)) return fail("independent radial chromatic interpolation");
  scene = std::make_shared<GaussianSceneData>(); scene->points = {point(3,{0.8F,0.4F,0.2F})}; camera.photo = {};
  for (const float saturation : {0.0F,0.5F,1.5F,2.0F}) {
    camera.photo.post.saturation = saturation;
    const auto image = renderer->render(scene,camera,true).image;
    Color rgb{decode(0.4),decode(0.2),decode(0.1)}; const double l = rgb[0]*0.2126+rgb[1]*0.7152+rgb[2]*0.0722;
    const auto pixel = image.pixelColor(128,128); const std::array<int,3> actual{pixel.red(),pixel.green(),pixel.blue()};
    for (size_t c = 0; c < 3; ++c) if (std::abs(actual[c]-std::lround(std::clamp(encode(std::max(0.0,l+(rgb[c]-l)*saturation)),0.0,1.0)*255)) > 1) return fail("independent saturation");
  }
  camera.photo.post = {2,16,2,32,4,15,0.7F,0.1F,2,1.1F};
  auto black = std::make_shared<GaussianSceneData>(); black->points = {point(-1,{1,1,1})};
  const auto darkness = renderer->render(black,camera,true);
  if (!darkness.error.isEmpty()) return fail("black render");
  for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) if (darkness.image.pixelColor(x,y) != QColor(Qt::black)) return fail("black remains black");
  const auto clean = [&] { auto c = camera; c.photo = {}; return renderer->render(scene,c,true).image; }();
  if (renderer->renderWithOptions(scene,camera,true,{},{{},{},true}).image != clean) return fail("explicit moving draft bypass");
  if (!evidence.isEmpty()) {
    if (!QDir().mkpath(evidence) || !base.image.save(QDir(evidence).filePath("neutral.png")) ||
        !bloom.image.save(QDir(evidence).filePath("bloom-hdr.png")) || !star.image.save(QDir(evidence).filePath("spectral-glare.png")) ||
        !plain.image.save(QDir(evidence).filePath("chromatic-neutral.png")) || !chromatic.image.save(QDir(evidence).filePath("chromatic.png"))) return fail("proof images");
    QFile receipt(QDir(evidence).filePath("receipt.json"));
    if (!receipt.open(QIODevice::WriteOnly)) return fail("proof receipt");
    receipt.write(QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"independentOracles",QVariantList{"HDR RGB bloom","spectral streaks","radial chromatic interpolation","linear saturation"}},
        {"toleranceBytes",2},{"scope","Synthetic GPU/image-space proof, not photographic reference parity"}}).toJson());
  }
  std::cout << "Camera post: independent HDR RGB bloom, spectral streaks, radial fringing, saturation, cutoff, native parity, black and draft checks pass\n";
  return true;
}
