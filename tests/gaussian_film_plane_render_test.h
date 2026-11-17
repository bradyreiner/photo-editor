#pragma once
#include "gaussian_film_plane.h"
#include "gaussian_physical_camera.h"
static bool gaussian_film_plane_regression(const QString& evidence={}) {
  const auto fail=[](const char* text) { std::cerr<<"Film plane: "<<text<<'\n'; return false; };
  auto renderer=make_gaussian_scene_renderer(); if (!renderer) return fail("Metal unavailable");
  GaussianCamera camera; camera.image_size={257,257}; camera.fx=camera.fy=240; camera.cx=camera.cy=128.5F;
  GaussianFilmSettings film; film.film_plane=true; film.grain_amount=0.8F; film.grain_microns=80;
  film.format_width_mm=film.format_height_mm=2; film.seed=123456;
  if (gaussian_film_settings(film.description())!=film) return fail("Film-plane strict round trip");
  for (int fault=0;fault<9;++fault) {
    auto broken=film.description();
    if (fault==0) broken["grainMicrons"]=0;
    if (fault==1) broken["halationMicrons"]=301;
    if (fault==2) broken["formatWidthMm"]=std::numeric_limits<double>::quiet_NaN();
    if (fault==3) broken["formatHeightMm"]="24";
    if (fault==4) broken["returnRgb"]=QVariantList{1,0};
    if (fault==5) broken["returnRgb"]=QVariantList{1,0,-0.001};
    if (fault==6) broken["returnRgb"]=QVariantList{1,"0.5",0};
    if (fault==7) broken["unknown"]=1;
    if (fault==8) broken.remove("formatWidthMm");
    if (gaussian_film_settings(broken)) return fail("Invalid physical film input accepted");
  }
  auto flat=std::make_shared<GaussianSceneData>();
  auto gray=point(3,{0.8F,0.8F,0.8F}); gray.covariance_a={10000,0,0,10000}; flat->points={gray};
  const auto clean=renderer->render(flat,camera,true);
  camera.photo.film=film;
  const auto grain=renderer->render(flat,camera,true);
  const auto preview=renderer->render(flat,camera,false);
  if (!clean.error.isEmpty() || !grain.error.isEmpty() || !preview.error.isEmpty() || grain.image==clean.image ||
      grain.image!=read_gaussian_photo_texture(preview) || grain.image!=renderer->render(flat,camera,true).image)
    return fail("Deterministic GPU grain / native texture parity");
  const auto decode=[](double c) { return c<=0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
  const auto encode=[](double c) { return c<=0.0031308 ? c*12.92 : 1.055*std::pow(c,1/2.4)-0.055; };
  const double pitch=2000.0/257;
  const auto spectrum=film_grain_spectrum(80,static_cast<float>(pitch),static_cast<float>(pitch),film.seed);
  const double variance=film_grain_variance(spectrum);
  int maximum_grain_error=0;
  for (int y=0;y<257;++y) for (int x=0;x<257;++x) {
    const double h=decode(clean.image.pixelColor(x,y).redF()),occupancy=h/(1+h);
    const double sigma=0.16*film.grain_amount*std::sqrt(occupancy*(1-occupancy))*std::log(10.0);
    const double noise=film_grain_value(spectrum,(x+0.5-128.5)*pitch,(y+0.5-128.5)*pitch);
    const int expected=static_cast<int>(std::lround(std::clamp(encode(h*std::exp(sigma*noise-0.5*sigma*sigma*variance)),0.0,1.0)*255));
    maximum_grain_error=std::max(maximum_grain_error,std::abs(expected-grain.image.pixelColor(x,y).red()));
    if (maximum_grain_error>1) return fail("GPU density grain differs from independent double reference");
  }
  ++camera.photo.film.seed;
  if (renderer->render(flat,camera,true).image==grain.image) return fail("New grain seed did not change realization");
  camera.photo.film=film; camera.photo.film.grain_amount=0;
  if (renderer->render(flat,camera,true).image!=clean.image) return fail("Film-plane zero exact bypass");
  GaussianPhysicalCameraSettings physical; physical.metres_per_scene_unit=1;
  physical.sensor_width_mm=physical.sensor_height_mm=2; physical.focus_distance_m=3; physical.f_number=64;
  auto physical_pose=camera; physical_pose.photo.film=film; physical_pose.photo.lens.samples=16;
  const auto physical_camera=make_gaussian_physical_camera(physical_pose,{257,257},physical);
  if (!physical_camera.camera) return fail("Physical recording gate setup");
  const auto calibrated=renderer->render(flat,*physical_camera.camera,true);
  auto unused_format=*physical_camera.camera;
  unused_format.photo.film.format_width_mm=180; unused_format.photo.film.format_height_mm=120;
  const auto inherited=renderer->render(flat,unused_format,true);
  if (!calibrated.error.isEmpty() || !inherited.error.isEmpty() || calibrated.image!=inherited.image)
    return fail("Calibrated sensor gate must override the standalone recording format");
  auto scene=std::make_shared<GaussianSceneData>();
  auto emitter=point(3,{1.8F,0.9F,0.4F}); emitter.covariance_a={0.0005F,0,0,0.0005F}; scene->points={emitter};
  camera.photo={}; const auto base=renderer->render(scene,camera,true);
  camera.photo.film=film; camera.photo.film.grain_amount=0; camera.photo.film.halation_amount=0.7F;
  camera.photo.film.halation_microns=40; camera.photo.film.return_rgb={0.8F,0.3F,0};
  const auto halo=renderer->render(scene,camera,true);
  if (!halo.error.isEmpty() || halo.image==base.image) return fail("RGB light return did not spread");
  auto no_return=camera; no_return.photo.film.return_rgb={0,0,0};
  if (renderer->render(scene,no_return,true).image!=base.image) return fail("Absorbed backing must produce no return");
  const double sigma=40/pitch; const int radius=static_cast<int>(std::ceil(3*sigma));
  using Color=std::array<double,3>;
  std::vector<Color> horizontal(257*257);
  std::vector<double> weights; double norm=0;
  for (int offset=-radius;offset<=radius;++offset) { const double w=std::exp(-0.5*offset*offset/(sigma*sigma)); weights.push_back(w); norm+=w; }
  for (auto& w:weights) w/=norm;
  const auto index=[](int x,int y) { return static_cast<size_t>(y*257+x); };
  for (int y=0;y<257;++y) for (int x=0;x<257;++x) for (int offset=-radius;offset<=radius;++offset) {
    if (x+offset<0 || x+offset>=257) continue;
    const auto rgb=base.image.pixelColor(x+offset,y);
    const Color input{decode(rgb.redF()),decode(rgb.greenF()),decode(rgb.blueF())};
    for (size_t c=0;c<3;++c) horizontal[index(x,y)][c]+=weights[static_cast<size_t>(offset+radius)]*input[c];
  }
  int maximum_halo_error=0;
  for (int y=0;y<257;++y) for (int x=0;x<257;++x) {
    Color blurred{};
    for (int offset=-radius;offset<=radius;++offset) if (y+offset>=0 && y+offset<257)
      for (size_t c=0;c<3;++c) blurred[c]+=weights[static_cast<size_t>(offset+radius)]*horizontal[index(x,y+offset)][c];
    const auto input=base.image.pixelColor(x,y),actual=halo.image.pixelColor(x,y);
    const Color h{decode(input.redF()),decode(input.greenF()),decode(input.blueF())};
    const std::array<int,3> pixel{actual.red(),actual.green(),actual.blue()};
    for (size_t c=0;c<3;++c) {
      const int expected=static_cast<int>(std::lround(std::clamp(encode(h[c]+0.7*camera.photo.film.return_rgb[c]*blurred[c]),0.0,1.0)*255));
      maximum_halo_error=std::max(maximum_halo_error,std::abs(pixel[c]-expected));
      if (maximum_halo_error>2) {
        std::cerr<<"diffusion x="<<x<<" y="<<y<<" channel="<<c<<" actual="<<pixel[c]<<" expected="<<expected
            <<" source="<<h[c]<<" blurred="<<blurred[c]<<" sigma="<<sigma<<'\n';
        return fail("Independent RGB linear-light diffusion reference");
      }
    }
    if (actual.blue()!=input.blue()) return fail("Zero blue return must not create blue halo");
  }
  camera.photo.film.halation_threshold=4;
  if (renderer->render(scene,camera,true).image!=halo.image) return fail("Physical light return must not use a luminance cutoff");
  camera.photo.film.grain_amount=0.8F;
  camera.photo.post.bloom_amount=0.2F; camera.photo.post.bloom_radius=3;
  const auto combined=renderer->render(scene,camera,true);
  const auto tiled=renderer->renderWithOptions(scene,camera,true,{},{{},{},false,64});
  if (!combined.error.isEmpty() || !tiled.error.isEmpty() || combined.image.size()!=tiled.image.size()) return fail("Combined tiled render failed");
  int tile_error=0;
  for (int y=0;y<257;++y) for (int x=0;x<257;++x) {
    const auto a=combined.image.pixelColor(x,y),b=tiled.image.pixelColor(x,y);
    tile_error=std::max({tile_error,std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
  }
  if (tile_error>1) return fail("Film-plane tile seams / global grain coordinates");
  auto black=std::make_shared<GaussianSceneData>(); black->points={point(-1,{1,1,1})};
  const auto darkness=renderer->render(black,camera,true);
  if (!darkness.error.isEmpty()) return fail("Black render failed");
  for (int y=0;y<257;++y) for (int x=0;x<257;++x)
    if (darkness.image.pixelColor(x,y)!=QColor(Qt::black)) return fail("Film plane creates light in black");
  // Record the cost of the post stage on a cached source separately from lens,
  // reconstruction, app interaction and display timing.
  auto performance=camera.resized({1536,1024}); performance.photo.post={};
  performance.photo.film.format_width_mm=36; performance.photo.film.format_height_mm=24;
  renderer->render(flat,performance,false);
  const auto cached=renderer->render(flat,performance,false);
  if (!cached.error.isEmpty() || !cached.metrics.value("resolvedSourceReused").toBool()) return fail("Film-plane source cache reuse");
  const QVariantMap receipt{{"passed",true},{"maxGrainByteError",maximum_grain_error},{"maxRgbDiffusionByteError",maximum_halo_error},
      {"maxTileByteError",tile_error},{"cached1536x1024GpuMs",cached.metrics.value("gpuMs")},
      {"scope","Synthetic GPU statistical density approximation and RGB Gaussian diffusion; not measured film stock or foreground interaction timing"}};
  if (!evidence.isEmpty()) {
    if (!QDir().mkpath(evidence) || !clean.image.save(QDir(evidence).filePath("neutral.png")) ||
        !grain.image.save(QDir(evidence).filePath("density-grain.png")) || !base.image.save(QDir(evidence).filePath("emitter.png")) ||
        !halo.image.save(QDir(evidence).filePath("rgb-halation.png")) || !combined.image.save(QDir(evidence).filePath("combined.png"))) return fail("Evidence images");
    QFile file(QDir(evidence).filePath("receipt.json"));
    if (!file.open(QIODevice::WriteOnly)) return fail("Evidence receipt"); file.write(QJsonDocument::fromVariant(receipt).toJson());
  }
  std::cout<<QJsonDocument::fromVariant(receipt).toJson().constData(); return true;
}
