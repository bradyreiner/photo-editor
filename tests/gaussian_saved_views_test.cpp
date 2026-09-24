#include "gaussian_saved_views.h"
#include "gaussian_physical_camera.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QLockFile>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  const auto project = QFileInfo(directory.path()).canonicalFilePath();
  const auto path = QDir(project).filePath("camera-views.json");
  const auto write = [](const QString& destination, const QByteArray& bytes) {
    QFile file(destination);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto read = [&] { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
  const auto check = [](bool ok, const char* message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
  };
  if (!write(QDir(project).filePath("scene.json"), "{}")) return 1;
  GaussianSceneData scene;
  GaussianCamera base; base.name = "capture.jpg"; base.image_size = {4208, 3120};
  base.fx = 3240.125F; base.fy = 3200.25F; base.cx = 2104.5F; base.cy = 1560.75F;
  base.position = {101.25F, -99.875F, 52.625F};
  scene.cameras = {base}; scene.artifact_sha256 = QString(64, 'a'); scene.focus_distance = 12.5F;
  auto camera = navigate_gaussian_camera(base, scene.focus_distance, 17.25F, -12.5F, .4F, .03F, -.07F);
  if (base.description().contains("photo") || !gaussian_saved_camera(base.description()) ||
      gaussian_saved_camera(base.description())->photo != GaussianPhotoSettings{}) return 1;
  camera.photo = {1.5F, -1.25F, 2};
  camera.photo.lens = {3, 0.2F, 7, 15, 0.2F, 64};
  camera.photo.film = {0.3F,2,0.4F,6,0.75F,4294967295U};
  for (const int samples : {16,32,64,128,256,512,1024,2048,4096}) {
    auto extended = camera; extended.photo.lens.samples = samples;
    const auto restored = gaussian_saved_camera(extended.description());
    if (!restored || restored->description() != extended.description()) return 1;
    auto wrong_version = extended.photo.lens.description();
    wrong_version["schema"] = samples > 128 ? "framehouse-thin-lens-v1" : "framehouse-thin-lens-v2";
    if (gaussian_lens_settings(wrong_version)) return 1;
  }
  for (const int samples : {0,17,129,255,257,1023,1025,2049,4097}) {
    auto bad = camera.photo.lens.description(); bad["samples"] = samples;
    bad["schema"] = samples > 128 ? "framehouse-thin-lens-v2" : "framehouse-thin-lens-v1";
    if (gaussian_lens_settings(bad)) return 1;
  }
  if (!check(load_gaussian_saved_views(project, scene).value("ok").toBool() && !QFileInfo::exists(path), "Reading empty project must not write")) return 1;
  const auto saved = save_gaussian_view(project, scene, "  Window light  ", 0, camera);
  if (!check(saved.value("ok").toBool(), "Save failed")) return 1;
  const auto original = read();
  if (QJsonDocument::fromJson(original).toVariant().toMap().value("schema") != "framehouse-gaussian-views-v2") return 1;
  const auto loaded = load_gaussian_saved_views(project, scene);
  const auto view = loaded.value("views").toList().value(0).toMap();
  const auto restored = gaussian_saved_camera(view.value("camera").toMap());
  if (!check(loaded.value("ok").toBool() && restored && restored->description() == camera.description() &&
      view.value("name").toString() == "Window light" && view.value("id") == saved.value("id"), "Camera must roundtrip exactly")) return 1;
  for (const auto& name : {QString("window LIGHT"), QString("\n"), QString(81, 'x'), QString("bad\tname")})
    if (!check(!save_gaussian_view(project, scene, name, 0, camera).value("ok").toBool() && read() == original, "Invalid/duplicate save changed file")) return 1;
  {
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(0) || save_gaussian_view(project, scene, "Concurrent", 0, camera).value("ok").toBool() || read() != original) return 1;
  }
  auto different = scene; different.cameras[0].fx += 1;
  if (load_gaussian_saved_views(project, different).value("ok").toBool() ||
      save_gaussian_view(project, different, "Changed calibration", 0, camera).value("ok").toBool() || read() != original) return 1;
  different = scene; different.artifact_sha256 = QString(64, 'b');
  if (load_gaussian_saved_views(project, different).value("ok").toBool()) return 1;
  for (int fault = 0; fault < 18; ++fault) {
    auto bad = camera.description();
    if (fault == 0) bad["fx"] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 1) bad["width"] = 1.5;
    if (fault == 2) bad["right"] = QVariantList{0, 0, 0};
    if (fault == 3) bad["forward"] = QVariantList{0, 0, -1};
    if (fault == 4) bad["position"] = QVariantList{1, "2", 3};
    if (fault == 5) bad["convention"] = "other";
    if (fault == 6) bad["fy"] = 0;
    if (fault == 7) bad["position"] = QVariantList{1, 2, std::numeric_limits<double>::infinity()};
    auto photo = camera.photo.description();
    if (fault == 8) photo["focalScale"] = 0.249999999;
    if (fault == 9) photo["exposureEv"] = 6.000000001;
    if (fault == 10) photo["look"] = "unknown-stock";
    if (fault == 11) photo["schema"] = "future-version";
    if (fault == 12) photo["exposureEv"] = "1";
    if (fault == 13) photo["exposureEv"] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 14) photo.remove("look");
    if (fault == 15) photo["unknown"] = 1;
    if (fault >= 8) bad["photo"] = photo;
    if (fault == 16) bad["photo"] = QVariant{};
    if (fault == 17) bad["photo"] = "invalid";
    if (!check(!gaussian_saved_camera(bad), "Invalid camera accepted")) return 1;
  }
  for (int fault = 0; fault < 12; ++fault) {
    auto bad = camera.description();
    auto photo = camera.photo.description();
    auto lens = camera.photo.lens.description();
    if (fault == 0) lens["focusDistance"] = 0;
    if (fault == 1) lens["apertureRadius"] = 1.0; // Exceeds focus/4.
    if (fault == 2) lens["focusDistance"] = std::numeric_limits<double>::infinity();
    if (fault == 3) lens["blades"] = 2;
    if (fault == 4) lens["blades"] = 6.5;
    if (fault == 5) lens["roundness"] = 1.001;
    if (fault == 6) lens["rotationDegrees"] = -1;
    if (fault == 7) lens["samples"] = 17;
    if (fault == 8) lens["samples"] = "32";
    if (fault == 9) lens["schema"] = "unknown";
    if (fault == 10) lens["unknown"] = 1;
    if (fault == 11) lens.remove("roundness");
    photo["lens"] = lens; bad["photo"] = photo;
    if (gaussian_saved_camera(bad)) return 1;
  }
  for (int fault = 0; fault < 13; ++fault) {
    auto photo = camera.photo.description();
    auto film = camera.photo.film.description();
    if (fault == 0) film["grainAmount"] = -0.01;
    if (fault == 1) film["grainSize"] = 0.499999999;
    if (fault == 2) film["halationAmount"] = 1.000000001;
    if (fault == 3) film["halationRadius"] = 17;
    if (fault == 4) film["halationThreshold"] = 0;
    if (fault == 5) film["seed"] = 4294967296.0;
    if (fault == 6) film["seed"] = 1.5;
    if (fault == 7) film["seed"] = -1;
    if (fault == 8) film["grainAmount"] = "0.5";
    if (fault == 9) film["grainSize"] = std::numeric_limits<double>::quiet_NaN();
    if (fault == 10) film["schema"] = "unknown";
    if (fault == 11) film["unknown"] = 1;
    if (fault == 12) film.remove("halationRadius");
    photo["film"] = film;
    if (gaussian_photo_settings(photo)) return 1;
  }
  for (int fault = 0; fault < 5; ++fault) {
    auto root = QJsonDocument::fromJson(original).toVariant().toMap();
    auto views = root.value("views").toList();
    auto invalid = views[0].toMap();
    if (fault == 0) invalid["captureCameraIndex"] = 1;
    if (fault == 1) invalid["id"] = "invalid";
    if (fault == 2) invalid["name"] = "  unnormalized  ";
    if (fault == 3) views.append(invalid);
    if (fault == 4) root["schema"] = "other";
    views[0] = invalid; root["views"] = views;
    const auto broken = QJsonDocument::fromVariant(root).toJson();
    if (!write(path, broken) || load_gaussian_saved_views(project, scene).value("ok").toBool() ||
        save_gaussian_view(project, scene, "New shot", 0, camera).value("ok").toBool() || read() != broken) return 1;
  }
  if (!write(path, "{broken") || load_gaussian_saved_views(project, scene).value("ok").toBool() ||
      !write(path, QByteArray(256 * 1024 + 1, ' ')) || load_gaussian_saved_views(project, scene).value("ok").toBool()) return 1;
  // Only this test's temporary sidecar is removed to exercise symlink protection.
  const auto external = QDir(project).filePath("untouched.json");
  if (!write(external, original) || !QFile::remove(path) || !QFile::link(external, path) ||
      load_gaussian_saved_views(project, scene).value("ok").toBool() ||
      save_gaussian_view(project, scene, "Symlink", 0, camera).value("ok").toBool() || read() != original) return 1;
  QTemporaryDir high_quality_directory;
  const auto high_quality_project = QFileInfo(high_quality_directory.path()).canonicalFilePath();
  auto high_quality_camera = camera; high_quality_camera.photo.lens.samples = 4096;
  if (!write(QDir(high_quality_project).filePath("scene.json"),"{}") ||
      !save_gaussian_view(high_quality_project,scene,"Final bokeh",0,high_quality_camera).value("ok").toBool()) return 1;
  const auto high_quality_loaded = load_gaussian_saved_views(high_quality_project,scene);
  const auto high_quality_view = high_quality_loaded.value("views").toList().value(0).toMap();
  const auto high_quality_restored = gaussian_saved_camera(high_quality_view.value("camera").toMap());
  if (!high_quality_loaded.value("ok").toBool() || !high_quality_restored ||
      high_quality_restored->description() != high_quality_camera.description()) return 1;
  GaussianPhysicalCameraSettings physical; physical.metres_per_scene_unit = 0.5;
  const auto physical_camera = make_gaussian_physical_camera(camera,camera.image_size,physical);
  if (!physical_camera.camera || !gaussian_saved_camera(physical_camera.camera->description()) ||
      !gaussian_saved_camera(physical_camera.camera->resized({123,97}).description())) return 1;
  if (!save_gaussian_view(high_quality_project,scene,"Physical camera",0,*physical_camera.camera).value("ok").toBool()) return 1;
  const auto physical_loaded = load_gaussian_saved_views(high_quality_project,scene);
  const auto physical_saved = physical_loaded.value("views").toList().value(1).toMap().value("camera").toMap();
  QFile physical_file(QDir(high_quality_project).filePath("camera-views.json"));
  if (!physical_file.open(QIODevice::ReadOnly) ||
      QJsonDocument::fromJson(physical_file.readAll()).object().value("schema") != "framehouse-gaussian-views-v3" ||
      !physical_loaded.value("ok").toBool() || physical_saved != physical_camera.camera->description()) return 1;
  for (int fault = 0; fault < 7; ++fault) {
    auto bad = physical_saved;
    if (fault == 0) bad["fx"] = bad.value("fx").toDouble()*1.01;
    if (fault == 1) bad["cx"] = bad.value("cx").toDouble()+1;
    auto calibration = bad.value("physicalCalibration").toMap();
    if (fault == 2) calibration["gateWidthMm"] = 0;
    if (fault == 3) calibration["scaleSource"] = "inferred";
    if (fault == 4) calibration["unknown"] = 1;
    if (fault == 5) { auto settings = calibration.value("settings").toMap(); settings["fNumber"] = 4; calibration["settings"] = settings; }
    bad["physicalCalibration"] = fault == 6 ? QVariant("invalid") : QVariant(calibration);
    if (gaussian_saved_camera(bad)) return 1;
  }
  std::cout << "Exact camera/extended-quality disk roundtrip, duplicate/corrupt/calibration/lock/size/symlink guards passed\n";
  return 0;
}
