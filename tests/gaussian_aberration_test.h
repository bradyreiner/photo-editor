#pragma once
#include "gaussian_lens_projection.h"
#include "gaussian_focus_pick.h"
#include "gaussian_scene_cleanup.h"
static bool gaussian_aberration_regression(const QString& output = {}) {
  const auto fail = [](const char* stage) { std::cerr << "Aberration: " << stage << '\n'; return false; };
  const auto decode = [](double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
  const auto encode = [](double c) { return c <= 0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; };
  GaussianCamera camera; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  for (const float k : {-0.5F,-0.2F,0.2F,0.5F}) {
    camera.photo.lens.spherical_distortion = k;
    for (int y = -20; y <= 90; y += 7) for (int x = -20; x <= 90; x += 7) {
      const QPointF pixel(x+0.37,y+0.61);
      const auto recovered = gaussian_lens_display_pixel(camera,gaussian_lens_chief_pixel(camera,pixel));
      if (std::hypot(recovered.x()-pixel.x(),recovered.y()-pixel.y()) > 1e-8) return fail("invertible projection");
    }
  }
  // Bounds must enclose every ray, including negative tile origins and points
  // outside the authored image. The bound does not depend on sampling quality.
  camera.photo.lens = {3,0.3F,7,25,0.2F,128}; camera.photo.lens.spherical_distortion = -0.5F;
  camera.photo.lens.astigmatism = -2; camera.photo.lens.field_curvature = 2;
  for (const QRect region : {QRect(-20,-10,41,37),QRect(0,0,65,65),QRect(40,25,50,60)}) {
    const auto bounds = gaussian_lens_source_region(camera,region,true); if (!bounds) return fail("coverage bounds");
    for (const auto p : gaussian_aperture_samples(camera.photo.lens)) for (int y = region.top(); y <= region.bottom(); y += 4) for (int x = region.left(); x <= region.right(); x += 4) {
      const auto ray = gaussian_lens_sample_pixel(camera,QPointF(x+0.5,y+0.5),QPointF(p.x(),p.y()));
      if (!QRectF(*bounds).adjusted(1,1,-1,-1).contains(ray)) return fail("ray escaped conservative coverage");
    }
  }
  auto renderer = make_gaussian_scene_renderer();
  auto scene = std::make_shared<GaussianSceneData>();
  auto background = point(6,{0.3F,0.5F,0.7F}); background.covariance_a = {5,0,0,5}; scene->points = {background,point(3,{0.9F,0.1F,0.05F})};
  auto composition = std::make_shared<GaussianSceneComposition>(); composition->ambient = 1;
  GaussianSceneObject object; object.id = "oracle-solid"; object.name = "Oracle solid"; object.position = {-0.6F,0.15F,2.5F}; object.size = {0.25F,0.3F,0.2F}; object.color = {0.2F,0.8F,0.4F}; composition->objects = {object};
  camera.photo = {}; camera.composition = composition;
  const auto base = renderer->render(scene,camera,true); if (!base.error.isEmpty()) return fail("neutral composite");
  const auto bilinear = [&](const QImage& image,double x,double y,int channel) {
    x = std::clamp(x,0.0,image.width()-1.0); y = std::clamp(y,0.0,image.height()-1.0);
    const int ix = static_cast<int>(x), iy = static_cast<int>(y); const double a = x-ix, b = y-iy;
    const auto value = [&](int px,int py) { const auto c = image.pixelColor(px,py); return decode(channel == 0 ? c.redF() : channel == 1 ? c.greenF() : c.blueF()); };
    return value(ix,iy)*(1-a)*(1-b)+value(std::min(ix+1,image.width()-1),iy)*a*(1-b)+
        value(ix,std::min(iy+1,image.height()-1))*(1-a)*b+value(std::min(ix+1,image.width()-1),std::min(iy+1,image.height()-1))*a*b;
  };
  camera.photo.lens.spherical_distortion = -0.5F;
  const auto warped = renderer->render(scene,camera,true);
  if (!warped.error.isEmpty() || warped.image == base.image) return fail("distorted splats and solid");
  for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) {
    // Independent double inverse radial map and linear reconstruction of the
    // neutral composed image, including the added object's silhouette.
    const double qx = (x-32.0)/60, qy = (y-32.0)/60, r2 = qx*qx+qy*qy;
    const double sx = 32+60*qx*(1-0.5*r2/(1+r2)), sy = 32+60*qy*(1-0.5*r2/(1+r2));
    const auto p = warped.image.pixelColor(x,y); const std::array<int,3> actual{p.red(),p.green(),p.blue()};
    for (int c = 0; c < 3; ++c) if (std::abs(actual[static_cast<size_t>(c)]-std::lround(encode(bilinear(base.image,sx,sy,c))*255)) > 2) return fail("independent chief-ray composite oracle");
  }
  if (read_gaussian_photo_texture(renderer->render(scene,camera,false)) != warped.image ||
      renderer->renderWithOptions(scene,camera,true,{},{{},{},true}).image != warped.image) return fail("native/draft distorted framing parity");
  // Lens distortion must still see samples beyond the original frame, rather
  // than clamp or fill black. This emitter is entirely outside the base view.
  auto beyond = std::make_shared<GaussianSceneData>(); auto emitter = point(3,{1,1,1}); emitter.center_opacity = {1.7F,0,3,0.999F};
  emitter.covariance_a = {1e-8F,0,0,1e-8F}; emitter.covariance_b = {0,1e-12F,0,0}; beyond->points = {emitter}; camera = {}; camera.image_size = {65,65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  const auto invisible = renderer->render(beyond,camera,true); camera.photo.lens.spherical_distortion = 0.5F;
  const auto visible = renderer->render(beyond,camera,true); int plain_peak = 0, warped_peak = 0;
  for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) { plain_peak = std::max(plain_peak,invisible.image.pixelColor(x,y).red()); warped_peak = std::max(warped_peak,visible.image.pixelColor(x,y).red()); }
  if (!visible.error.isEmpty() || plain_peak != 0 || warped_peak < 80 || visible.metrics.value("lensSourceWidth").toInt() <= 65) { std::cerr << "Off-frame base=" << plain_peak << " mapped=" << warped_peak << " width=" << visible.metrics.value("lensSourceWidth").toInt() << " error=" << visible.error.toStdString() << "\n"; return fail("off-frame coverage and expanded culling"); }
  // A constant scene must remain constant through warped borders, in both signs.
  auto flat = std::make_shared<GaussianSceneData>(); auto gray = point(3,{1,1,1}); gray.covariance_a = {1e9F,0,0,1e9F}; flat->points = {gray};
  for (const float k : {-0.5F,0.5F}) {
    camera.photo.lens.spherical_distortion = k; const auto image = renderer->render(flat,camera,true).image;
    for (int y = 0; y < 65; ++y) for (int x = 0; x < 65; ++x) if (std::abs(image.pixelColor(x,y).red()-128) > 1) return fail("no black/clamped warped border");
  }
  // Independently render each pinhole pupil ray, then apply the lens direction
  // field in double precision and integrate. This includes splat/solid occlusion.
  camera.photo = {}; camera.composition = composition; camera.photo.lens = {3,0.2F,0,0,0,16};
  camera.photo.lens.astigmatism = 0.8F; camera.photo.lens.field_curvature = -0.5F;
  const auto aberrated = renderer->render(scene,camera,true); if (!aberrated.error.isEmpty()) return fail("aberrated pupil composite");
  std::vector<std::array<double,3>> mean(65*65);
  for (const auto p : gaussian_aperture_samples(camera.photo.lens)) {
    auto plain_camera = gaussian_aperture_camera(camera,p); plain_camera.photo.lens.aperture_radius = 0; plain_camera.photo.lens.astigmatism = plain_camera.photo.lens.field_curvature = 0;
    const auto ray = renderer->render(scene,plain_camera,true); if (!ray.error.isEmpty()) return fail("independent pupil camera");
    for (int y = 3; y < 62; ++y) for (int x = 3; x < 62; ++x) {
      const double qx = (x-32.0)/60, qy = (y-32.0)/60, r2 = qx*qx+qy*qy, b = r2/(1+r2);
      const double dot = qx*p.x()+qy*p.y(), ax = r2 == 0 ? 0 : 2*qx*dot/r2-p.x(), ay = r2 == 0 ? 0 : 2*qy*dot/r2-p.y();
      const double sx = x-60*b*(-0.5*p.x()+0.8*ax)/3, sy = y-60*b*(-0.5*p.y()+0.8*ay)/3;
      for (int c = 0; c < 3; ++c) mean[static_cast<size_t>(y*65+x)][static_cast<size_t>(c)] += bilinear(ray.image,sx,sy,c)/16;
    }
  }
  for (int y = 3; y < 62; ++y) for (int x = 3; x < 62; ++x) {
    const auto p = aberrated.image.pixelColor(x,y); const std::array<int,3> actual{p.red(),p.green(),p.blue()};
    for (size_t c = 0; c < 3; ++c) if (std::abs(actual[c]-std::lround(encode(mean[static_cast<size_t>(y*65+x)][c])*255)) > 2) return fail("independent per-pupil aberration oracle");
  }
  // Focus picking and rectangle selection must address the distorted picture.
  camera.photo.lens = {}; camera.photo.lens.spherical_distortion = 0.5F; camera.photo.lens.field_curvature = 0.4F; camera.composition.reset();
  const auto display = gaussian_lens_display_pixel(camera,QPointF(66.5,32.5));
  const auto focus = pick_gaussian_focus(*beyond,camera,display,{},{});
  const auto q = gaussian_lens_field(camera,display); const double expected_focus = 3*(1+0.9*gaussian_lens_field_weight(q));
  if (!focus.value("ok").toBool() || std::abs(focus.value("surfaceDepth").toDouble()-3) > 1e-5 || std::abs(focus.value("focusDistance").toDouble()-expected_focus) > 1e-5) return fail("distorted focus-pick and curved focus depth");
  const QRectF rect((display.x()-1)/65,(display.y()-1)/65,2.0/65,2.0/65);
  if (select_gaussian_cleanup(*beyond,QByteArray(1,'\1'),camera,rect,false) != QByteArray(1,'\0')) return fail("distorted cleanup selection");
  // Curvature/astigmatism cannot alter pinhole pixels when distortion is zero.
  camera.photo = {}; camera.composition = composition; const auto unchanged = renderer->render(scene,camera,true);
  camera.photo.lens.astigmatism = 2; camera.photo.lens.field_curvature = -2;
  if (renderer->render(scene,camera,true).image != unchanged.image) return fail("pinhole aberration bypass");
  auto large = camera.resized({2049,257}); large.photo = {}; large.photo.lens = {3,0.05F,0,0,0,16}; large.photo.lens.spherical_distortion = 0.2F;
  const auto progressive = std::make_shared<GaussianProgressiveFrames>(); GaussianRenderOptions large_options; large_options.progressive_frames = progressive;
  const auto native_tiled = renderer->renderWithOptions(scene,large,false,{},large_options);
  const auto photo_tiled = renderer->render(scene,large,true);
  if (!native_tiled.error.isEmpty() || !photo_tiled.error.isEmpty() || !native_tiled.image.isNull() || !native_tiled.native_texture ||
      !native_tiled.metrics.value("nativeTiledLens").toBool() || native_tiled.metrics.value("previewReadback").toBool() ||
      native_tiled.metrics.value("tileCount").toInt() != 3 || progressive->takeLatest() ||
      read_gaussian_photo_texture(native_tiled) != photo_tiled.image) return fail("native tiled assembly/readback parity without partial-tile publication");
  if (!output.isEmpty()) {
    if (!QDir().mkpath(output) || !base.image.save(QDir(output).filePath("neutral-composite.png")) || !warped.image.save(QDir(output).filePath("distorted-composite.png")) ||
        !aberrated.image.save(QDir(output).filePath("aberrated-composite.png")) || !visible.image.save(QDir(output).filePath("beyond-frame.png"))) return fail("proof images");
    auto grid = std::make_shared<GaussianSceneData>(); camera = {}; camera.image_size = {513,513}; camera.fx = camera.fy = 400; camera.cx = camera.cy = 256.5F;
    camera.photo.exposure_ev = 6; camera.photo.lens = {3,0.3F,0,0,0,2048};
    for (const float x : {-160.0F,0.0F,160.0F}) for (const float y : {-160.0F,0.0F,160.0F}) {
      auto p = point(4,{1,1,1}); p.center_opacity = {x*4/400,y*4/400,4,0.999F}; p.covariance_a = {0.0001F,0,0,0.0001F}; p.covariance_b = {0,1e-12F,0,0}; grid->points.push_back(p);
    }
    for (int mode = 0; mode < 4; ++mode) {
      camera.photo.lens.astigmatism = mode == 1 ? -1.5F : mode == 2 ? 1.5F : 0;
      camera.photo.lens.field_curvature = mode == 3 ? 1.5F : 0;
      camera.photo.lens.spherical_distortion = mode == 3 ? 0.3F : 0;
      const auto frame = renderer->render(grid,camera,true);
      if (!frame.error.isEmpty() || !frame.image.save(QDir(output).filePath(QString("bokeh-grid-%1.png").arg(mode)))) return fail("bokeh grid render");
    }
    QFile receipt(QDir(output).filePath("receipt.json")); if (!receipt.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return fail("receipt");
    receipt.write(QJsonDocument::fromVariant(QVariantMap{{"passed",true},{"schema","framehouse-aberration-proof-v1"},{"offFrameBasePeak",plain_peak},{"offFrameMappedPeak",warped_peak},
        {"oracleToleranceBytes",2},{"scope","Analytic lens mapping and bilinear per-ray reconstruction, not calibrated glass or Octane pixel parity"}}).toJson());
  }
  std::cout << "Aberration: invertible projection, conservative coverage, independent chief/per-pupil image oracles, off-frame geometry, focus and cleanup pass\n";
  return true;
}
