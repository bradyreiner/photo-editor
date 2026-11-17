#pragma once
#include "gaussian_lens_sampling.h"
#include "gaussian_saved_views.h"

static bool gaussian_creative_camera_regression(const QString& output = {}) {
  const auto fail = [](const char* message) { std::cerr << "Creative camera: " << message << '\n'; return false; };
  GaussianToneSettings tone; tone.warmth = 0.4F; tone.tint = -0.1F; tone.vibrance = 0.3F; tone.fade = 0.03F;
  if (gaussian_tone_settings(tone.description()) != tone ||
      GaussianToneSettings{}.description().value("schema") != "framehouse-camera-tone-v1") return fail("color persistence / legacy identity");
  auto invalid = tone.description(); invalid["warmth"] = "0.4";
  if (gaussian_tone_settings(invalid)) return fail("color number validation");
  invalid = tone.description(); invalid["fade"] = 0.3;
  if (gaussian_tone_settings(invalid)) return fail("color bounds");
  GaussianLensSettings lens; lens.aperture_radius = 0.2F; lens.samples = 1024; lens.bokeh_aspect = 2;
  if (gaussian_lens_settings(lens.description()) != lens) return fail("oval pupil persistence");
  const auto oval_samples = gaussian_aperture_samples(lens);
  lens.samples = 128;
  const auto prefix = gaussian_aperture_samples(lens);
  if (!std::equal(prefix.begin(),prefix.end(),oval_samples.begin())) return fail("oval progressive prefixes");
  double xx = 0, yy = 0;
  for (const auto p : oval_samples) { xx += p.x()*p.x(); yy += p.y()*p.y(); }
  if (std::abs(std::sqrt(yy/xx)-2) > 0.02 ||
      std::abs(std::sqrt(xx*yy)/static_cast<double>(oval_samples.size()) - 0.01) > 0.0002) return fail("equal-area ellipse moments");
  auto scene = std::make_shared<GaussianSceneData>();
  GaussianPoint hidden; hidden.center_opacity = {0,0,-3,0.9F};
  hidden.covariance_a = {0.01F,0,0,0.01F}; hidden.covariance_b = {0,0.01F,0,0}; scene->points = {hidden};
  GaussianCamera camera; camera.image_size = {129,129}; camera.fx = camera.fy = 180; camera.cx = camera.cy = 64.5F;
  GaussianSceneComposition composition; composition.ambient = 1;
  GaussianSceneObject plane; plane.id = "chart"; plane.name = "Neutral chart"; plane.kind = 1;
  plane.position = {0,0,3}; plane.size = {5,5,0.001F}; plane.color = {0.5F,0.5F,0.5F}; composition.objects = {plane};
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  auto renderer = make_gaussian_scene_renderer();
  const auto neutral = renderer->render(scene,camera,true);
  camera.photo.tone.warmth = 0.8F;
  const auto warm = renderer->render(scene,camera,true); const auto wp = warm.image.pixelColor(64,64);
  if (!warm.error.isEmpty() || wp.red() <= neutral.image.pixelColor(64,64).red()+8 ||
      wp.blue() >= neutral.image.pixelColor(64,64).blue()-8 || !warm.metrics.value("resolvedSourceReused").toBool())
    return fail("warm/cool balance or cached color edit");
  camera.photo.tone = {}; camera.photo.tone.tint = 0.8F;
  const auto tinted = renderer->render(scene,camera,true); const auto tp = tinted.image.pixelColor(64,64);
  if (!tinted.error.isEmpty() || tp.red() <= tp.green()+8 || tp.blue() <= tp.green()+8 ||
      read_gaussian_photo_texture(renderer->render(scene,camera,false)) != tinted.image) return fail("tint / native texture parity");
  camera.photo.tone = {};
  if (renderer->render(scene,camera,true).image != neutral.image) return fail("neutral color exact bypass");
  composition.objects[0].color = {0.5F,0.42F,0.38F};
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto muted = renderer->render(scene,camera,true);
  camera.photo.tone.vibrance = 1;
  const auto vibrant = renderer->render(scene,camera,true);
  const auto mp=muted.image.pixelColor(64,64),vp=vibrant.image.pixelColor(64,64);
  if (!vibrant.error.isEmpty() || vp.red()-vp.blue() <= mp.red()-mp.blue()+5) return fail("vibrance response");
  camera.photo.tone = {};
  camera.composition.reset(); camera.photo.tone.fade = 0.04F;
  const auto faded = renderer->render(scene,camera,true);
  if (!faded.error.isEmpty() || faded.image.pixelColor(64,64).red() < 50) return fail("faded black response");
  camera.photo = {};
  composition.objects[0].size = {0.035F,0.035F,0.001F}; composition.objects[0].color = {1,1,1};
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto source = renderer->render(scene,camera,true);
  auto& film = camera.photo.film; film.film_plane = true; film.highlight_halation = true;
  film.format_width_mm = film.format_height_mm = 2; film.halation_amount = 0.9F; film.halation_microns = 90;
  film.halation_threshold = 4;
  if (gaussian_film_settings(film.description()) != film || renderer->render(scene,camera,true).image != source.image)
    return fail("highlight cutoff must reject subthreshold light / persist");
  film.halation_threshold = 0.1F;
  const auto glow = renderer->render(scene,camera,true);
  if (!glow.error.isEmpty() || glow.image.pixelColor(68,64).red() <= source.image.pixelColor(68,64).red()+5 ||
      glow.image.pixelColor(68,64).red() <= glow.image.pixelColor(68,64).green()) return fail("visible red highlight halo");
  const auto tiled = renderer->renderWithOptions(scene,camera,true,{},{{},{},false,64});
  if (!tiled.error.isEmpty()) return fail("highlight halation tiled render");
  for (int y=0;y<129;++y) for (int x=0;x<129;++x) {
    const auto a=glow.image.pixelColor(x,y),b=tiled.image.pixelColor(x,y);
    if (std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())})>1)
      return fail("highlight halo tile overlap");
  }
  camera.photo = {}; camera.photo.lens = {2,0.35F,0,0,0,1024};
  camera.photo.lens.bokeh_aspect = 2;
  composition.objects[0].position.setZ(6); composition.objects[0].size = {0.025F,0.025F,0.001F};
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto oval = renderer->render(scene,camera,true);
  const auto moments = [](const QImage& image) {
    double energy=0, x2=0, y2=0;
    for (int y=0;y<129;++y) for (int x=0;x<129;++x) {
      const double c=image.pixelColor(x,y).redF(), weight=c<=0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4);
      energy+=weight; x2+=weight*(x-64)*(x-64); y2+=weight*(y-64)*(y-64);
    }
    return std::array<double,2>{energy,std::sqrt(y2/x2)};
  };
  camera.photo.lens.bokeh_aspect = 1;
  const auto round = renderer->render(scene,camera,true);
  if (!oval.error.isEmpty() || !round.error.isEmpty() || oval.image.isNull() ||
      std::abs(moments(oval.image)[1]-2)>0.12 || std::abs(moments(round.image)[1]-1)>0.08 ||
      std::abs(moments(oval.image)[0]/moments(round.image)[0]-1)>0.1) return fail("rendered oval bokeh shape / energy");
  if (!output.isEmpty()) {
    if (QFileInfo::exists(output) || !QDir().mkpath(output) || !warm.image.save(output+"/warm.png") ||
        !tinted.image.save(output+"/tint.png") || !source.image.save(output+"/halation-off.png") ||
        !glow.image.save(output+"/halation-on.png") || !oval.image.save(output+"/oval.png") || !round.image.save(output+"/round.png")) return fail("evidence images");
    QFile receipt(output+"/receipt.json");
    const QVariantMap data{{"passed",true},{"ovalBokehAspect",moments(oval.image)[1]},
        {"roundBokehAspect",moments(round.image)[1]},{"ovalToRoundEnergy",moments(oval.image)[0]/moments(round.image)[0]},
        {"scope","Creative color, equal-area elliptical pupil and highlight-mask film-plane approximation; not full anamorphic glass or a measured film stock"}};
    if (!receipt.open(QIODevice::WriteOnly) || receipt.write(QJsonDocument::fromVariant(data).toJson())<0) return false;
  }
  std::cout << "Creative color, cached edits, native texture, highlight halo, tiles and oval bokeh passed\n";
  return true;
}
