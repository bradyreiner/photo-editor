#include "gaussian_scene_renderer.h"
#include <QGuiApplication>
#include <QImage>
#include <iostream>

using namespace photo_editor::desktop;

QImage read_gaussian_photo_texture(const GaussianFrame& frame);

namespace {
GaussianPoint colored_point(QVector3D center, QVector3D color, float opacity, float sigma) {
  GaussianPoint point;
  point.center_opacity = {center.x(),center.y(),center.z(),opacity};
  point.covariance_a = {sigma*sigma,0,0,sigma*sigma};
  point.covariance_b = {0,sigma*sigma,0,0};
  for (size_t channel = 0; channel < 3; ++channel)
    point.sh[channel] = (color[static_cast<int>(channel)]-0.5F)/0.2820947918F;
  return point;
}

GaussianCamera view_camera(QSize size) {
  GaussianCamera camera;
  camera.image_size = size;
  camera.fx = camera.fy = static_cast<float>(size.width()) * 0.92F;
  camera.cx = static_cast<float>(size.width()) * 0.5F;
  camera.cy = static_cast<float>(size.height()) * 0.5F;
  camera.position = {2,0,-3};
  const QVector3D target(0,0,-0.01F);
  camera.forward = (target-camera.position).normalized();
  camera.right = QVector3D::crossProduct({0,1,0},camera.forward).normalized();
  camera.down = QVector3D::crossProduct(camera.forward,camera.right).normalized();
  camera.photo.lens.focus_distance = (target-camera.position).length();
  camera.photo.lens.aperture_radius = 0.04F;
  camera.photo.lens.samples = 32;
  camera.photo.lens.ray_traced = true;
  return camera;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc,argv);
  const auto fail = [](const char* stage) { std::cerr << "Ray scene: " << stage << '\n'; return 1; };
  constexpr int edge = 33;
  const auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return fail("Metal renderer unavailable");

  // An ordinary metallic solid reflects a captured Gaussian through the new
  // primary-ray spatial path, while the same rays carry per-segment haze.
  auto camera = view_camera({edge,edge});
  const QVector3D hit(0,0,-0.01F);
  auto reflection = camera.forward; reflection.setZ(-reflection.z());
  auto scene = std::make_shared<GaussianSceneData>();
  scene->points = {colored_point(hit+reflection*1.5F,{0.12F,0.35F,0.95F},0.95F,0.35F)};
  GaussianSceneComposition composition;
  composition.ambient = 0;
  composition.reflections = true; composition.spatial_reflections = true;
  composition.environment_diffuse = true; composition.spatial_environment = true;
  composition.environment_strength = 0.6F;
  composition.haze_density = 0.025F; composition.haze_color = {0.08F,0.1F,0.16F};
  composition.haze_shadows = true; composition.scan_shadows = true;
  composition.light_samples = 16;
  GaussianSceneObject mirror;
  mirror.id = "ray-primary-mirror"; mirror.name = "Primary ray mirror"; mirror.kind = 1;
  mirror.position = hit; mirror.size = {3,3,0.01F}; mirror.color = {0.85F,0.85F,0.85F};
  mirror.roughness = 0.06F; mirror.metallic = 0.85F;
  composition.objects = {mirror};
  const auto render_spatial = [&](const GaussianSceneComposition& value) {
    auto input = camera;
    input.composition = std::make_shared<GaussianSceneComposition>(value);
    return renderer->render(scene,input,true);
  };
  const auto spatial = render_spatial(composition);
  if (!spatial.error.isEmpty() || spatial.image.isNull() ||
      !spatial.metrics.value("rayTracedPrimary").toBool() ||
      spatial.metrics.value("primaryRaySamplesPerPixel").toInt() != camera.photo.lens.samples ||
      spatial.metrics.value("primaryRayTransportModel") != "linear-radiance-spatial-path-gaussian-over-scene-v1" ||
      spatial.metrics.value("reflectionModel") != "spatial-gaussian-peak-ordered-ggx-v1" ||
      spatial.metrics.value("environmentLightingModel") != "surface-cosine-spatial-gaussian-v1" ||
      spatial.metrics.value("hazeModel") != "primary-ray-homogeneous-single-scattering-v1" ||
      spatial.metrics.value("solidObjectCount").toInt() != 1 ||
      spatial.metrics.value("scanShadowParticleCount").toInt() != 1)
    return fail("ray path did not report ordinary-solid, spatial, haze and scan execution");
  const auto center_color = spatial.image.pixelColor(edge/2,edge/2);
  if (center_color.red()+center_color.green()+center_color.blue() < 12)
    return fail("spatial primary rays produced no reflected Gaussian radiance");

  auto no_spatial = composition;
  no_spatial.reflections = false; no_spatial.spatial_reflections = false;
  no_spatial.environment_diffuse = false; no_spatial.spatial_environment = false;
  no_spatial.haze_density = 0; no_spatial.haze_shadows = false; no_spatial.scan_shadows = false;
  const auto pinhole_solid = render_spatial(no_spatial);
  if (!pinhole_solid.error.isEmpty() || pinhole_solid.image == spatial.image)
    return fail("spatial reflection / haze edits did not change the primary-ray image");
  auto clear_haze = composition;
  clear_haze.haze_density = 0; clear_haze.haze_shadows = false; clear_haze.scan_shadows = false;
  const auto no_haze = render_spatial(clear_haze);
  if (!no_haze.error.isEmpty() || no_haze.image == spatial.image)
    return fail("per-segment haze did not affect the ray-traced composition");

  // A camera-visible area lamp is an ordinary primary intersection. Its
  // visibility toggle must change actual pixels as well as the route metrics.
  auto lamp_camera = view_camera({edge,edge});
  lamp_camera.position = {0,0,0}; lamp_camera.right = {1,0,0}; lamp_camera.down = {0,1,0};
  lamp_camera.forward = {0,0,1}; lamp_camera.fx = lamp_camera.fy = 30;
  lamp_camera.cx = lamp_camera.cy = edge*0.5F;
  lamp_camera.photo.lens.focus_distance = 2;
  auto empty = std::make_shared<GaussianSceneData>();
  empty->points = {colored_point({0,0,8},{0,0,0},0,0.2F)};
  GaussianSceneComposition lamp_scene;
  GaussianSceneLight lamp;
  lamp.id = "visible-ray-lamp"; lamp.name = "Visible ray lamp"; lamp.kind = 3;
  lamp.vector = {0,0,1.5F}; lamp.radius = 0.3F; lamp.color = {1,0.18F,0.04F};
  lamp.intensity = 1; lamp.camera_visible = true;
  lamp_scene.lights = {lamp};
  const auto render_lamp = [&](bool visible) {
    auto value = lamp_scene; value.lights[0].camera_visible = visible;
    lamp_camera.composition = std::make_shared<GaussianSceneComposition>(value);
    return renderer->render(empty,lamp_camera,true);
  };
  const auto visible_lamp = render_lamp(true), hidden_lamp = render_lamp(false);
  if (!visible_lamp.error.isEmpty() || !hidden_lamp.error.isEmpty() ||
      visible_lamp.metrics.value("cameraVisibleEmitterCount").toInt() != 1 ||
      hidden_lamp.metrics.value("cameraVisibleEmitterCount").toInt() != 0 ||
      visible_lamp.image.pixelColor(edge/2,edge/2).red() < 200 ||
      visible_lamp.image == hidden_lamp.image)
    return fail("camera-visible emitter was not intersected by primary rays");

  // The worker publishes immutable power-of-two ray prefixes. Repeat the same
  // target to verify deterministic 16-sample prefixes, require the prefix to
  // differ from the 32-sample final, and compare that final with an export.
  const auto progressive_renderer = make_gaussian_scene_renderer();
  const auto prefixes = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions progressive_options; progressive_options.progressive_frames = prefixes;
  camera.composition = std::make_shared<GaussianSceneComposition>(composition);
  const auto progressive = progressive_renderer->renderWithOptions(scene,camera,false,{},progressive_options);
  const auto prefix = prefixes->takeLatest();
  if (!progressive.error.isEmpty() || !prefix ||
      prefix->metrics.value("primaryRaySamplesPerPixel").toInt() != 16 ||
      prefix->metrics.value("primaryRayCount").toLongLong() != edge*edge*16 ||
      progressive.metrics.value("progressiveFramesProduced").toInt() < 2 ||
      !progressive.metrics.value("primaryRayCaptureExecuted").toBool())
    return fail("expected the completed 16-sample primary-ray prefix");
  const auto prefix_pixels = read_gaussian_photo_texture(*prefix);
  const auto repeated_prefixes = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions repeated_options; repeated_options.progressive_frames = repeated_prefixes;
  const auto repeated_progressive = make_gaussian_scene_renderer()->renderWithOptions(scene,camera,false,{},repeated_options);
  const auto repeated_prefix = repeated_prefixes->takeLatest();
  auto full_quality = camera; full_quality.photo.lens.samples = 32;
  const auto full_reference = make_gaussian_scene_renderer()->render(scene,full_quality,true);
  if (!repeated_progressive.error.isEmpty() || !repeated_prefix || !full_reference.error.isEmpty() ||
      repeated_prefix->metrics.value("primaryRaySamplesPerPixel").toInt() != 16 ||
      prefix_pixels.isNull() || prefix_pixels != read_gaussian_photo_texture(*repeated_prefix) ||
      prefix_pixels == full_reference.image ||
      read_gaussian_photo_texture(*prefix) != prefix_pixels ||
      read_gaussian_photo_texture(progressive) != full_reference.image)
    return fail("same-target prefixes were nondeterministic, aliased the final image or changed after publication");

  // At this area, one fixed six-sample dispatch cannot land on any progressive
  // power of two. The renderer must shorten batches at the next prefix boundary.
  auto large_camera = view_camera({400,400});
  large_camera.photo.lens.focus_distance = 4;
  large_camera.photo.lens.aperture_radius = 0.1F;
  large_camera.photo.lens.samples = 16;
  auto large_scene = std::make_shared<GaussianSceneData>();
  large_scene->points = {colored_point(large_camera.position+large_camera.forward*6,
      {0.9F,0.35F,0.12F},0.95F,6/large_camera.fx)};
  const auto large_queue = std::make_shared<GaussianProgressiveFrames>();
  GaussianRenderOptions large_options; large_options.progressive_frames = large_queue;
  const auto large_final = make_gaussian_scene_renderer()->renderWithOptions(
      large_scene,large_camera,false,{},large_options);
  const auto large_prefix = large_queue->takeLatest();
  constexpr qint64 large_pixels = 400LL*400LL;
  if (!large_final.error.isEmpty() || !large_prefix || !large_prefix->native_texture ||
      large_prefix->metrics.value("primaryRaySamplesPerPixel").toInt() != 8 ||
      large_prefix->metrics.value("primaryRayCount").toLongLong() != large_pixels*8 ||
      large_final.metrics.value("primaryRaySamplesPerPixel").toInt() != 16 ||
      large_final.metrics.value("primaryRayCount").toLongLong() != large_pixels*16 ||
      large_final.metrics.value("primaryRaysTraced").toLongLong() != large_pixels*16 ||
      large_final.metrics.value("progressiveFramesProduced").toInt() < 2)
    return fail("400x400 ray batches skipped the 8-sample progressive prefix");

  std::cout << "Ray scene: solid reflection, camera-visible emitter, per-segment haze, spatial paths and progressive texture parity pass.\n";
  return 0;
}
