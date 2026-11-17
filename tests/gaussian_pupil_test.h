#pragma once
// Geometry/throughput oracles and a rendered point-light grid qualify the new
// pupil behavior. They do not stand in for a measured lens or reference footage.
static bool gaussian_pupil_regression(const QString& output = {}) {
  const auto fail = [](const char* stage) { std::cerr << "Pupil: " << stage << '\n'; return false; };
  const auto decode = [](double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
  const auto encode = [](double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; };
  GaussianLensSettings lens{3,0.3F,0,0,0,4096};
  for (const float edge : {0.0F,1.0F,3.0F,8.0F}) {
    lens.aperture_edge = edge;
    const auto samples = gaussian_aperture_samples(lens);
    if (samples.size() != 4096) return fail("complete radial sampling");
    double moment = 0;
    for (const auto p : samples) moment += p.lengthSquared()/(lens.aperture_radius*lens.aperture_radius)/4096;
    if (std::abs(moment-(edge+2)/(edge+4)) > 0.002) return fail("independent radial pupil moment");
    for (const double r : {0.25,0.5,0.75,0.9}) {
      const auto count = std::count_if(samples.begin(),samples.end(),[&](const auto p) { return p.length() < r*lens.aperture_radius; });
      if (std::abs(static_cast<double>(count)/4096-std::pow(r,edge+2)) > 0.003) return fail("independent radial pupil CDF");
    }
    for (const int blades : {3,6,16}) {
      auto polygon = lens; polygon.blades = blades;
      if (gaussian_aperture_samples(polygon).size() != 4096) return fail("full polygon quality at extreme edge response");
      polygon.samples = 128; const auto prefix = gaussian_aperture_samples(polygon); polygon.samples = 4096;
      const auto complete = gaussian_aperture_samples(polygon);
      if (!std::equal(prefix.begin(),prefix.end(),complete.begin())) return fail("stable importance-sampling prefixes");
    }
  }
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  camera.photo.lens = {3,0.3F,0,0,0,256};
  auto flat = std::make_shared<GaussianSceneData>(); auto gray = point(3,{1,1,1});
  gray.covariance_a = {1e9F,0,0,1e9F}; flat->points = {gray};
  camera.photo.lens.vignetting_distance = 0.45F;
  auto renderer = make_gaussian_scene_renderer(); const auto clipped = renderer->render(flat,camera,true);
  if (!clipped.error.isEmpty()) return fail("barrel render");
  const auto samples = gaussian_aperture_samples(camera.photo.lens);
  int corners_darkened = 0;
  for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) {
    // Trace a ray from each aperture point to the chosen point on the focus
    // plane, intersect the barrel plane, and test its actual opening radius.
    // This calculation uses scene-space double geometry, not shader uniforms.
    const double target_x = (x+0.5-camera.cx)/camera.fx*3, target_y = (y+0.5-camera.cy)/camera.fy*3;
    int accepted = 0;
    for (const auto p : samples) {
      const double t = static_cast<double>(camera.photo.lens.vignetting_distance)/3;
      const double bx = p.x()+(target_x-p.x())*t, by = p.y()+(target_y-p.y())*t;
      const double radius = camera.photo.lens.aperture_radius;
      if (bx*bx+by*by <= radius*radius) ++accepted;
    }
    const auto expected = std::lround(encode(decode(0.5)*accepted/256)*255);
    if (std::abs(clipped.image.pixelColor(x,y).red()-expected) > 1) return fail("independent barrel-plane throughput");
    if (expected < 100) ++corners_darkened;
  }
  if (clipped.image.pixelColor(32,32).red() < 127 || corners_darkened < 50) return fail("center preservation and corner throughput loss");
  auto scaled = std::make_shared<GaussianSceneData>(*flat);
  for (auto& p : scaled->points) { for (int axis = 0; axis < 3; ++axis) p.center_opacity[static_cast<size_t>(axis)] *= 10; for (auto& c : p.covariance_a) c *= 100; for (auto& c : p.covariance_b) c *= 100; }
  auto scaled_camera = camera; scaled_camera.photo.lens.focus_distance *= 10; scaled_camera.photo.lens.aperture_radius *= 10; scaled_camera.photo.lens.vignetting_distance *= 10;
  if (renderer->render(scaled,scaled_camera,true).image != clipped.image) return fail("scene-unit scale invariance");
  // Analytic overlap of two disks, independent of the deterministic sample set.
  // A forward barrel sees a contracted aperture disk of radius a at its plane.
  camera.photo.lens.samples = 4096;
  const auto converged = renderer->render(flat,camera,true);
  const double a = camera.photo.lens.aperture_radius*(1-camera.photo.lens.vignetting_distance/3), b = camera.photo.lens.aperture_radius;
  const double d = camera.photo.lens.vignetting_distance*32/60;
  const double area = a*a*std::acos((d*d+a*a-b*b)/(2*d*a))+b*b*std::acos((d*d+b*b-a*a)/(2*d*b))-
      0.5*std::sqrt((-d+a+b)*(d+a-b)*(d-a+b)*(d+a+b));
  const double transmission = area/(M_PI*a*a);
  const double actual = decode(converged.image.pixelColor(64,32).redF())/decode(0.5);
  if (std::abs(actual-transmission) > 0.012) return fail("analytic two-disk overlap convergence");
  // Opening the barrel restores throughput and must invalidate the source cache.
  camera.photo.lens.vignetting_scale = 4;
  const auto opened = renderer->render(flat,camera,true);
  if (opened.metrics.value("resolvedSourceReused").toBool() || opened.image.pixelColor(0,0).red() < 127) return fail("barrel opening and cache invalidation");
  camera.photo.lens.aperture_edge = 3;
  const auto rim_flat = renderer->render(flat,camera,true);
  if (rim_flat.image != opened.image) return fail("edge response must not change uniform-image brightness");
  // Actual defocused point: compare an inner disk and outer annulus in linear
  // light, and ensure the edge control redistributes rather than creates energy.
  auto scene = std::make_shared<GaussianSceneData>(); auto source = point(1.5F,{1,1,1}); source.center_opacity[3] = 0.999F;
  source.covariance_a = {0.00005625F,0,0,0.00005625F}; source.covariance_b = {0,1e-12F,0,0}; scene->points = {source};
  camera.image_size = {129,129}; camera.fx = camera.fy = 200; camera.cx = camera.cy = 64.5F;
  camera.photo.lens = {3,0.3F,0,0,0,2048}; camera.photo.exposure_ev = 4;
  const auto uniform = renderer->render(scene,camera,true);
  camera.photo.lens.aperture_edge = 3;
  const auto rim = renderer->render(scene,camera,true);
  const auto stats = [&](const QImage& image) {
    double energy = 0, inner = 0, outer = 0; int ni = 0, no = 0;
    for (int y = 0; y < 129; ++y) for (int x = 0; x < 129; ++x) {
      const double v = decode(image.pixelColor(x,y).redF()); energy += v;
      const int r2 = (x-64)*(x-64)+(y-64)*(y-64);
      if (r2 < 36) { inner += v; ++ni; }
      if (r2 >= 196 && r2 <= 324) { outer += v; ++no; }
    }
    return std::array<double,3>{energy,inner/ni,outer/no};
  };
  const auto before = stats(uniform.image), after = stats(rim.image);
  if (!uniform.error.isEmpty() || !rim.error.isEmpty() || std::abs(after[0]/before[0]-1) > 0.03 ||
      after[2]/after[1] < 5 || after[2] <= before[2]*1.2) return fail("rendered rim/center contrast and energy");
  const auto queue = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions options; options.progressive_frames = queue;
  camera.photo.lens.samples = 128; camera.photo.lens.vignetting_distance = 0.7F;
  const auto fresh = make_gaussian_scene_renderer();
  const auto final = fresh->renderWithOptions(scene,camera,false,{},options);
  const auto partial = queue->takeLatest();
  auto prefix_camera = camera; prefix_camera.photo.lens.samples = 64;
  if (!final.error.isEmpty() || !partial || partial->metrics.value("apertureSamples").toInt() != 64 ||
      read_gaussian_photo_texture(*partial) != renderer->render(scene,prefix_camera,true).image ||
      read_gaussian_photo_texture(final) != renderer->render(scene,camera,true).image) return fail("immutable exact pupil prefixes");
  camera.photo.post.bloom_amount = 0.5F;
  if (!fresh->render(scene,camera,true).metrics.value("resolvedSourceReused").toBool()) return fail("post edits reuse resolved pupil image");
  camera.photo = {}; const auto pinhole = renderer->render(scene,camera,true);
  camera.photo.lens.aperture_edge = 8; camera.photo.lens.vignetting_distance = 0.2F; camera.photo.lens.vignetting_scale = 0.5F;
  if (renderer->render(scene,camera,true).image != pinhole.image) return fail("zero aperture exact bypass");
  if (!output.isEmpty()) {
    if (!QDir().mkpath(output) || !uniform.image.save(QDir(output).filePath("uniform.png")) || !rim.image.save(QDir(output).filePath("edge-response.png")) ||
        !clipped.image.save(QDir(output).filePath("barrel-throughput.png"))) return fail("proof images");
    // Larger photographic bokeh grid for visual inspection; proof-only cost.
    auto grid = std::make_shared<GaussianSceneData>(); camera = {}; camera.image_size = {513,513};
    camera.fx = camera.fy = 400; camera.cx = camera.cy = 256.5F; camera.photo.exposure_ev = 6; camera.photo.lens = {3,0.3F,0,0,0,4096};
    for (const float x : {-160.0F,0.0F,160.0F}) for (const float y : {-160.0F,0.0F,160.0F}) {
      auto p = point(8,{1,1,1}); p.center_opacity = {x*8/400,y*8/400,8,0.999F}; p.covariance_a = {0.0004F,0,0,0.0004F}; p.covariance_b = {0,1e-12F,0,0}; grid->points.push_back(p);
    }
    for (int mode = 0; mode < 3; ++mode) {
      camera.photo.lens.aperture_edge = mode == 0 ? 0.0F : 3.0F;
      camera.photo.lens.vignetting_distance = mode == 2 ? 0.75F : 0.0F;
      const auto frame = renderer->render(grid,camera,true);
      if (!frame.error.isEmpty() || !frame.image.save(QDir(output).filePath(QString("bokeh-grid-%1.png").arg(mode)))) return fail("bokeh grid");
    }
    QFile receipt(QDir(output).filePath("receipt.json"));
    if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return fail("receipt");
    receipt.write(QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"schema","framehouse-pupil-proof-v1"},
        {"analyticBarrelTransmission",transmission},{"renderedBarrelTransmission",actual},{"uniformEnergy",before[0]},{"edgeEnergy",after[0]},
        {"edgeOuterInnerRatio",after[2]/after[1]},{"scope","Synthetic radial pupil and circular barrel model, not measured lens or photographic parity"}}).toJson());
  }
  std::cout << "Pupil: radial CDF/moments, barrel-plane and disk-overlap oracles, rim redistribution, source invalidation, prefixes and bypass pass\n";
  return true;
}
