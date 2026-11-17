#include "gaussian_camera_path.h"
#include "gaussian_saved_views.h"
#include "gaussian_physical_camera.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QQuaternion>
#include <iostream>
#include <cmath>
#include <limits>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  GaussianCamera a; a.name = "A"; a.image_size = {800, 600}; a.fx = a.fy = 600; a.cx = 400; a.cy = 300;
  auto b = a; b.name = "B"; b.position = {4, 2, 0}; b.fx = 800;
  b.photo = {2, 2, 0};
  const auto rotation = QQuaternion::fromAxisAndAngle({0, 1, 0}, 90);
  b.right = rotation.rotatedVector(a.right); b.down = rotation.rotatedVector(a.down); b.forward = rotation.rotatedVector(a.forward);
  const QVariantList views{QVariantMap{{"id", "a"}, {"camera", a.description()}}, QVariantMap{{"id", "b"}, {"camera", b.description()}}};
  const QVariantMap settings{{"viewIds", QVariantList{"a", "b", "a"}}, {"segmentSeconds", 1.0}, {"fps", 24}};
  const auto path = gaussian_camera_path(settings, views);
  if (!path || path->duration() != 2 || path->frameCount() != 48 || path->sample(0).description() != a.description() ||
      path->sample(1).description() != b.description() || path->sample(2).description() != a.description() ||
      path->sample(-1).description() != a.description() || path->sample(3).description() != a.description()) return 1;
  const auto middle = path->sample(.5);
  if ((middle.position - QVector3D(2, 1, 0)).length() > 1e-6F || middle.fx != 700 ||
      middle.photo.focal_scale != 1.5F || middle.photo.exposure_ev != 1 ||
      (middle.forward - QVector3D(std::sqrt(.5F), 0, std::sqrt(.5F))).length() > 1e-5F) return 1;
  for (int i = 0; i <= 2000; ++i) if (!gaussian_saved_camera(path->sample(i / 1000.0).description())) return 1;
  // Rotations on opposite sides of the Euler wrap take the short two-degree arc.
  auto left = a, right = a;
  const auto turn = [](GaussianCamera& camera, float degrees) {
    const auto q = QQuaternion::fromAxisAndAngle({0, 1, 0}, degrees);
    camera.right = q.rotatedVector({1, 0, 0}); camera.down = q.rotatedVector({0, 1, 0}); camera.forward = q.rotatedVector({0, 0, 1});
  };
  turn(left, 179); turn(right, -179);
  GaussianCameraPath wrapped; wrapped.cameras = {left, right}; wrapped.segment_seconds = 1;
  if (wrapped.sample(.5).forward.z() > -.999F) return 1;
  for (int fault = 0; fault < 7; ++fault) {
    auto bad = settings;
    if (fault == 0) bad["fps"] = 29.97;
    if (fault == 1) bad["segmentSeconds"] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 2) bad["viewIds"] = QVariantList{"a"};
    if (fault == 3) bad["viewIds"] = QVariantList{"a", "a"};
    if (fault == 4) bad["viewIds"] = QVariantList{"a", "missing"};
    if (fault == 5) bad["segmentSeconds"] = 61;
    if (fault == 6) bad["fps"] = "24";
    if (gaussian_camera_path(bad, views)) return 1;
  }
  auto stretched = views; auto bad_b = b; bad_b.image_size = {900, 600};
  stretched[1] = QVariantMap{{"id", "b"}, {"camera", bad_b.description()}};
  if (gaussian_camera_path(settings, stretched)) return 1;
  auto mixed_looks = views; auto different_look = b; different_look.photo.look = 1;
  mixed_looks[1] = QVariantMap{{"id", "b"}, {"camera", different_look.description()}};
  if (gaussian_camera_path(settings, mixed_looks)) return 1;
  GaussianPhysicalCameraSettings physical; physical.metres_per_scene_unit = 0.5;
  const auto physical_a = make_gaussian_physical_camera(a,a.image_size,physical);
  const auto physical_b = make_gaussian_physical_camera(b,a.image_size,physical);
  if (!physical_a.camera || !physical_b.camera) return 1;
  QVariantList physical_views{QVariantMap{{"id","a"},{"camera",physical_a.camera->description()}},
      QVariantMap{{"id","b"},{"camera",physical_b.camera->description()}}};
  const auto physical_path = gaussian_camera_path(settings,physical_views);
  if (!physical_path || !gaussian_physical_camera_consistent(physical_path->sample(0.5)) ||
      !gaussian_saved_camera(physical_path->sample(0.5).resized({333,251}).description())) return 1;
  physical.focus_distance_m = 3;
  physical_views[1] = QVariantMap{{"id","b"},{"camera",make_gaussian_physical_camera(b,a.image_size,physical).camera->description()}};
  const auto manual_focus_path=gaussian_camera_path(settings,physical_views);
  if (!manual_focus_path || manual_focus_path->sample(0.5).physical->settings.focus_distance_m!=2.5 ||
      !gaussian_physical_camera_consistent(manual_focus_path->sample(0.5))) return 1;
  physical.focal_length_mm=135;
  auto zoom_end=make_gaussian_physical_camera(b,a.image_size,physical);
  if (!zoom_end.camera) return 1;
  physical_views[1]=QVariantMap{{"id","b"},{"camera",zoom_end.camera->description()}};
  const auto zoom_path=gaussian_camera_path(settings,physical_views);
  if (!zoom_path) return 1;
  for (int i=0;i<=100;++i) {
    const double time=i/100.0,t=time*time*(3-2*time);
    const auto sample=zoom_path->sample(time);
    const double focal=50+(135-50)*t,focus=2+t;
    const double image_distance=focal*focus*1000/(focus*1000-focal);
    if (!sample.physical || !gaussian_saved_camera(sample.description()) || !gaussian_physical_camera_consistent(sample) ||
        std::abs(sample.physical->settings.focal_length_mm-focal)>1e-4 ||
        std::abs(sample.fx-image_distance*sample.image_size.width()/sample.physical->gate_width_mm)>0.002 ||
        std::abs(sample.photo.lens.aperture_radius-focal/(2000*physical.f_number*physical.metres_per_scene_unit))>1e-7) return 1;
  }
  auto wrong_scale=physical; wrong_scale.metres_per_scene_unit=1;
  auto incompatible=physical_views;
  incompatible[1]=QVariantMap{{"id","b"},{"camera",make_gaussian_physical_camera(b,a.image_size,wrong_scale).camera->description()}};
  if (gaussian_camera_path(settings,incompatible)) return 1;
  physical.focal_length_mm=50;
  physical_views[1]=QVariantMap{{"id","b"},{"camera",make_gaussian_physical_camera(b,a.image_size,physical).camera->description()}};
  for (auto& value : physical_views) {
    auto view = value.toMap(); auto camera = gaussian_saved_camera(view.value("camera").toMap());
    if (!camera) return 1;
    camera->photo.lens.auto_focus = true; view["camera"] = camera->description(); value = view;
  }
  const auto physical_af_path = gaussian_camera_path(settings,physical_views);
  if (!physical_af_path || physical_af_path->sample(0.5).physical->settings.focus_distance_m != 2.5) return 1;
  for (int i = 0; i <= 100; ++i) if (!gaussian_saved_camera(physical_af_path->sample(i/100.0).description())) return 1;
  auto film_views = views; auto film_a = a, film_b = b;
  film_b.photo.film.grain_amount = 0.3F;
  film_views[1] = QVariantMap{{"id", "b"}, {"camera", film_b.description()}};
  if (gaussian_camera_path(settings, film_views)) return 1;
  film_a.photo.film = film_b.photo.film;
  film_views[0] = QVariantMap{{"id", "a"}, {"camera", film_a.description()}};
  const auto film_path = gaussian_camera_path(settings, film_views);
  if (!film_path || film_path->sample(0.5).photo.film != film_a.photo.film) return 1;
  film_b.photo.post.glare_amount = 0.5F;
  film_views[1] = QVariantMap{{"id", "b"}, {"camera", film_b.description()}};
  if (gaussian_camera_path(settings,film_views)) return 1;
  film_a.photo.post = film_b.photo.post;
  film_views[0] = QVariantMap{{"id", "a"}, {"camera", film_a.description()}};
  const auto post_path = gaussian_camera_path(settings,film_views);
  if (!post_path || post_path->sample(0.5).photo.post != film_a.photo.post) return 1;
  {
    auto tone_views = views; auto tone_a = a, tone_b = b;
    tone_a.photo.tone = {0.5F,0.2F,-0.5F,-0.2F,-0.3F,0.02F}; tone_b.photo.tone = {1.5F,0.8F,0.5F,0.2F,0.3F,0.08F};
    tone_views[0] = QVariantMap{{"id","a"},{"camera",tone_a.description()}};
    tone_views[1] = QVariantMap{{"id","b"},{"camera",tone_b.description()}};
    const auto tone_path = gaussian_camera_path(settings,tone_views);
    if (!tone_path || std::abs(tone_path->sample(0.5).photo.tone.contrast-1) > 1e-6F ||
        std::abs(tone_path->sample(0.5).photo.tone.highlight_compression-0.5F) > 1e-6F ||
        std::abs(tone_path->sample(0.5).photo.tone.warmth)>1e-6F ||
        std::abs(tone_path->sample(0.5).photo.tone.fade-0.05F)>1e-6F) return 1;
    for (int i = 0; i <= 200; ++i) if (!gaussian_saved_camera(tone_path->sample(i/100.0).description())) return 1;
  }
  for (int feature = 0; feature < 10; ++feature) {
    auto lens_views = film_views; auto changed = film_b;
    if (feature == 0) changed.photo.lens.aperture_edge = 3;
    if (feature == 1) changed.photo.lens.vignetting_distance = 0.1F;
    if (feature == 2) changed.photo.lens.vignetting_scale = 1.2F;
    if (feature == 3) changed.photo.lens.spherical_distortion = 0.2F;
    if (feature == 4) changed.photo.lens.astigmatism = -0.5F;
    if (feature == 5) changed.photo.lens.field_curvature = 0.3F;
    if (feature == 6) changed.photo.lens.auto_focus = true;
    if (feature == 7) changed.photo.lens.bokeh_aspect = 2;
    if (feature == 8) changed.photo.lens.ray_traced = true;
    if (feature == 9) changed.photo.lens.pupil_softness = 1.5F;
    lens_views[1] = QVariantMap{{"id","b"},{"camera",changed.description()}};
    if (gaussian_camera_path(settings,lens_views)) return 1;
    auto matching = film_a; matching.photo.lens = changed.photo.lens;
    lens_views[0] = QVariantMap{{"id","a"},{"camera",matching.description()}};
    const auto lens_path = gaussian_camera_path(settings,lens_views);
    if (!lens_path || lens_path->sample(0.5).photo.lens != matching.photo.lens) return 1;
  }
  film_a.photo.lens.samples = film_b.photo.lens.samples = 4096;
  film_views[0] = QVariantMap{{"id", "a"}, {"camera", film_a.description()}};
  film_views[1] = QVariantMap{{"id", "b"}, {"camera", film_b.description()}};
  const auto high_quality_path = gaussian_camera_path(settings,film_views);
  if (!high_quality_path || high_quality_path->sample(0.5).photo.lens.samples != 4096 ||
      !gaussian_saved_camera(high_quality_path->sample(0.5).description())) return 1;
  auto optical_views = views;
  auto optical_b = b; optical_b.photo.lens = {5, 0.25F, 0, 0, 0, 32};
  optical_views[1] = QVariantMap{{"id", "b"}, {"camera", optical_b.description()}};
  const auto optical_path = gaussian_camera_path(settings, optical_views);
  if (!optical_path || optical_path->sample(.5).photo.lens.focus_distance != 3 ||
      optical_path->sample(.5).photo.lens.aperture_radius != 0.125F) return 1;
  optical_b.photo.lens.blades = 7;
  optical_views[1] = QVariantMap{{"id", "b"}, {"camera", optical_b.description()}};
  if (gaussian_camera_path(settings, optical_views)) return 1;
  QTemporaryDir directory; const auto project = QFileInfo(directory.path()).canonicalFilePath();
  QFile manifest(QDir(project).filePath("scene.json"));
  if (!manifest.open(QIODevice::WriteOnly) || manifest.write("{}") != 2) return 1;
  manifest.close();
  GaussianSceneData scene; scene.cameras = {a}; scene.artifact_sha256 = QString(64, 'a');
  const auto first = save_gaussian_view(project, scene, "First", 0, a);
  const auto second = save_gaussian_view(project, scene, "Second", 0, b);
  const QVariantMap saved_settings{{"viewIds", QVariantList{first.value("id"), second.value("id")}}, {"segmentSeconds", 1.0}, {"fps", 24}};
  if (!save_gaussian_camera_path(project, scene, saved_settings).value("ok").toBool() ||
      !save_gaussian_view(project, scene, "Third", 0, a).value("ok").toBool()) return 1;
  const auto loaded = load_gaussian_saved_views(project, scene);
  if (!loaded.value("ok").toBool() || loaded.value("cameraPath").toMap() != saved_settings ||
      !gaussian_camera_path(loaded.value("cameraPath").toMap(), loaded.value("views").toList())) return 1;
  auto invalid = saved_settings; invalid["viewIds"] = QVariantList{"missing", "shot"};
  if (save_gaussian_camera_path(project, scene, invalid).value("ok").toBool() ||
      load_gaussian_saved_views(project, scene).value("cameraPath").toMap() != saved_settings) return 1;
  std::cout << "Path endpoints, easing, shortest arc, 2001 orthonormal samples, lens-model compatibility and persistence passed\n";
}
