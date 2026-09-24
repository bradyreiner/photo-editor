#include "gaussian_scene_renderer.h"
#include "gaussian_ply_validation.h"
#include "gaussian_scalar_reference.h"
#include <QGuiApplication>
#include <QColorSpace>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDataStream>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace photo_editor::desktop;
QImage read_gaussian_photo_texture(const GaussianFrame& frame);
#include "gaussian_lens_test.h"
static GaussianPoint point(float z, std::array<float, 3> color) {
  GaussianPoint p;
  p.center_opacity = {0, 0, z, 0.5F};
  p.covariance_a = {0.09F, 0, 0, 0.09F}; p.covariance_b = {0, 0.09F, 0, 0};
  for (size_t i = 0; i < 3; ++i) p.sh[i] = (color[i] - 0.5F) / 0.2820947918F;
  return p;
}
#include "gaussian_film_effects_test.h"
#include "gaussian_film_perf.h"
#include "gaussian_resolved_source_test.h"
#include "gaussian_tiled_photo_test.h"
#include "gaussian_bokeh_quality_test.h"
#include "gaussian_optical_precision_test.h"
static bool parser_regression() {
  QTemporaryDir temp;
  const auto write = [&](const QString& name, const QByteArray& bytes) {
    QFile file(QDir(temp.path()).filePath(name));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  QByteArray cameras;
  QDataStream camera_stream(&cameras, QIODevice::WriteOnly); camera_stream.setByteOrder(QDataStream::LittleEndian);
  camera_stream << quint64{1} << quint32{11} << qint32{1} << quint64{65} << quint64{65} << 60.0 << 60.0 << 32.5 << 32.5;
  QByteArray images;
  QDataStream image_stream(&images, QIODevice::WriteOnly); image_stream.setByteOrder(QDataStream::LittleEndian);
  image_stream << quint64{1} << quint32{37} << 1.0 << 0.0 << 0.0 << 0.0 << 0.0 << 0.0 << 0.0 << quint32{11};
  image_stream.writeRawData("proof.png\0", 10); image_stream << quint64{0};
  QStringList properties{"x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2"};
  for (int i = 0; i < 45; ++i) properties.append(QStringLiteral("f_rest_%1").arg(i));
  properties.sort();  // Lexicographic order deliberately differs from SH basis order.
  QByteArray ply("ply\nformat binary_little_endian 1.0\nelement vertex 1\n");
  for (const auto& property : properties) ply += "property float " + property.toUtf8() + '\n';
  ply += "end_header\n";
  QByteArray payload;
  QDataStream values(&payload, QIODevice::WriteOnly); values.setByteOrder(QDataStream::LittleEndian);
  values.setFloatingPointPrecision(QDataStream::SinglePrecision);
  for (const auto& property : properties) {
    const float value = property == "z" ? 3.0F : property == "rot_0" ? 1.0F : property.startsWith("scale_") ? std::log(0.3F) :
        property == "f_rest_31" ? 0.5F : property == "f_rest_0" ? 0.25F : 0.0F;
    values << value;
  }
  ply += payload;
  if (!write("cameras.bin", cameras) || !write("images.bin", images) || !write("scene.ply", ply)) return false;
  const auto path = QDir(temp.path()).filePath("scene.ply");
  const auto hash = QString::fromLatin1(QCryptographicHash::hash(ply, QCryptographicHash::Sha256).toHex());
  const auto loaded = load_gaussian_scene(path, temp.path(), hash);
  if (!loaded.scene || loaded.scene->cameras.size() != 1 || loaded.scene->cameras[0].name != "proof.png" ||
      loaded.scene->sh_degree != 3 || loaded.scene->points.size() != 1 ||
      loaded.scene->points[0].sh[3] != 0.25F || loaded.scene->points[0].sh[8] != 0.5F ||
      std::abs(loaded.scene->points[0].covariance_a[0] - 0.09F) > 1e-6F) return false;
  if (load_gaussian_scene(path, temp.path(), QString(64, '0')).scene) return false;
  images.chop(1);
  return write("images.bin", images) && !load_gaussian_scene(path, temp.path(), hash).scene;
}
static bool photo_processing_regression(const std::shared_ptr<GaussianSceneRenderer>& renderer) {
  GaussianCamera camera; camera.image_size = {65, 65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
  auto scene = std::make_shared<GaussianSceneData>();
  scene->points = {point(3, {0.8F, 0.4F, 0.2F})};
  const auto neutral = renderer->render(scene, camera, true);
  if (!neutral.error.isEmpty()) return false;
  // Independent double-precision color calculation at the exact splat center
  // (alpha 0.5). A +1 EV edit must not simply double gamma-encoded bytes.
  for (const float ev : {-6.0F, -1.0F, 0.0F, 0.25F, 1.0F, 6.0F}) for (int look = 0; look < 3; ++look) {
    camera.photo = {1, ev, look};
    const auto frame = renderer->render(scene, camera, true);
    if (!frame.error.isEmpty() || frame.image.colorSpace() != QColorSpace(QColorSpace::SRgb)) return false;
    std::array<double, 3> expected{0.4, 0.2, 0.1};
    for (auto& c : expected) c = (c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4)) * std::exp2(ev);
    if (look == 2) expected.fill(expected[0] * 0.2126 + expected[1] * 0.7152 + expected[2] * 0.0722);
    const double peak = *std::max_element(expected.begin(), expected.end());
    if (look && peak > 0.6) for (auto& c : expected) c *= (0.6 + 0.4 * -std::expm1(-(peak - 0.6) / 0.4)) / peak;
    const auto pixel = frame.image.pixelColor(32, 32);
    const std::array<int, 3> actual{pixel.red(), pixel.green(), pixel.blue()};
    for (size_t channel = 0; channel < 3; ++channel) {
      const double c = expected[channel];
      const double encoded = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
      if (std::abs(actual[channel] - std::lround(std::clamp(encoded, 0.0, 1.0) * 255)) > 1) {
        std::cerr << "Photo processing mismatch: EV=" << ev << " look=" << look << '\n'; return false;
      }
    }
    const auto preview = renderer->render(scene, camera, false);
    if (!preview.error.isEmpty() || !preview.image.isNull() || !preview.native_texture ||
        preview.metrics.value("photoSettings") != frame.metrics.value("photoSettings") ||
        read_gaussian_photo_texture(preview) != frame.image) return false;
  }
  camera.photo = {};
  if (renderer->render(scene, camera, true).image != neutral.image) return false;
  // Lens framing changes projected size, not camera pose, and survives resizing.
  camera.photo.focal_scale = 2;
  const auto zoomed = renderer->render(scene, camera, true);
  if (!zoomed.error.isEmpty() || zoomed.image.pixelColor(42, 32).red() <= neutral.image.pixelColor(42, 32).red() + 10 ||
      camera.resized({130, 130}).photo != camera.photo) return false;
  camera.photo.exposure_ev = std::numeric_limits<float>::quiet_NaN();
  if (renderer->render(scene, camera, false).error.isEmpty()) return false;
  // Exercise both transfer-function branches, black, the knee, and retained
  // over-white reconstruction values. Exposure must run before final clipping.
  for (const float encoded : {0.0F, 0.02F, 0.04045F, 0.05F, 1.2F}) for (const float ev : {-1.0F, 1.0F}) {
    auto gray_scene = std::make_shared<GaussianSceneData>();
    gray_scene->points = {point(3, {2 * encoded, 2 * encoded, 2 * encoded})};
    camera.photo = {1, ev, 0};
    const auto frame = renderer->render(gray_scene, camera, true);
    const double linear = (encoded <= 0.04045F ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4)) * std::exp2(ev);
    const double expected = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
    if (!frame.error.isEmpty() || std::abs(frame.image.pixelColor(32, 32).red() -
        std::lround(std::clamp(expected, 0.0, 1.0) * 255)) > 1) return false;
  }
  std::cout << "18 independent GPU exposure/look checks, neutral identity, lens projection and texture-only preview pass\n";
  return true;
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  auto renderer = make_gaussian_scene_renderer();
  if (!renderer) return 1;
  const auto args = app.arguments();
  if (args.value(1) == "--film-perf" || args.value(1) == "--motion-perf") return gaussian_film_perf(args);
  if (args.value(1) == "--bokeh-quality-proof" && args.size() == 3) return gaussian_bokeh_quality_regression(args[2]) ? 0 : 1;
  if (args.value(1) == "--optical-precision-proof" && args.size() == 3) return gaussian_optical_precision_regression(args[2]) ? 0 : 1;
  if (args.size() == 1) {
    if (!photo_processing_regression(renderer)) return 1;
    if (!gaussian_lens_regression(renderer)) return 1;
    if (!gaussian_bokeh_quality_regression()) return 1;
    if (!gaussian_optical_precision_regression()) return 1;
    if (!gaussian_film_effects_regression(renderer)) return 1;
    if (!gaussian_resolved_source_regression()) return 1;
    if (!gaussian_tiled_photo_regression()) return 1;
    if (!parser_regression()) { std::cerr << "Gaussian/camera parser regression failed"; return 1; }
    auto scene = std::make_shared<GaussianSceneData>();
    GaussianCamera camera; camera.image_size = {65, 65}; camera.fx = camera.fy = 60; camera.cx = camera.cy = 32.5F;
    scene->points = {point(2, {1, 0, 0}), point(4, {0, 0, 1})};
    auto frame = renderer->render(scene, camera, true);
    if (!frame.error.isEmpty()) { std::cerr << frame.error.toStdString(); return 1; }
    const auto center = frame.image.pixelColor(32, 32);
    if (std::abs(center.red() - 128) > 2 || center.green() > 2 || std::abs(center.blue() - 64) > 2) {
      std::cerr << "Depth/alpha/channel mismatch: " << center.name().toStdString(); return 1;
    }
    auto anisotropic = std::make_shared<GaussianSceneData>();
    auto oval = point(3, {1, 1, 1}); oval.center_opacity[3] = 1;
    oval.covariance_a[0] = 0.36F; oval.covariance_a[3] = 0.01F;
    anisotropic->points = {oval};
    frame = renderer->render(anisotropic, camera, true);
    if (!frame.error.isEmpty() || frame.image.pixelColor(40, 32).red() < 150 || frame.image.pixelColor(32, 40).red() > 10) return 1;
    auto behind = std::make_shared<GaussianSceneData>(); behind->points = {point(-3, {1, 1, 1})};
    frame = renderer->render(behind, camera, true);
    if (!frame.error.isEmpty() || frame.image.pixelColor(32, 32).red() != 0) return 1;
    auto termination = std::make_shared<GaussianSceneData>();
    auto near = point(2, {1, 0, 0}); near.center_opacity[3] = 0.999F;
    auto middle = point(3, {0, 0, 1}); middle.center_opacity[3] = 0.99F;
    termination->points = {point(4, {0, 1, 0}), near, middle};
    frame = renderer->render(termination, camera, true);
    const auto stopped = frame.image.pixelColor(32, 32);
    if (!frame.error.isEmpty() || stopped.red() < 253 || stopped.blue() > 1 || stopped.green() > 1) return 1;
    // Fully terminated pixels stay unchanged even under dense bright overdraw,
    // while the same rear splats still contribute outside the occluded center.
    // This guards per-fragment termination, not whole-splat culling.
    const auto occluded_image = frame.image;
    auto dense = std::make_shared<GaussianSceneData>(*termination);
    for (int i = 0; i < 128; ++i) {
      auto rear = point(5 + static_cast<float>(i) * 0.01F, {0, 0, 8});
      rear.center_opacity[3] = 0.5F;
      rear.covariance_a[0] = rear.covariance_a[3] = 0.5F;
      dense->points.push_back(rear);
    }
    const auto overdraw = renderer->render(dense, camera, true);
    if (!overdraw.error.isEmpty() || overdraw.image.pixelColor(32, 32) != stopped ||
        overdraw.image.pixelColor(50, 32).blue() <= occluded_image.pixelColor(50, 32).blue() + 20) {
      std::cerr << "Occlusion termination changed an opaque pixel or culled visible rear contributions"; return 1;
    }
    auto harmonics = std::make_shared<GaussianSceneData>(); harmonics->sh_degree = 1;
    auto colored = point(3, {0.5F, 0.5F, 0.5F}); colored.center_opacity[3] = 1; colored.sh[6] = 0.5F;
    harmonics->points = {colored};
    frame = renderer->render(harmonics, camera, true);
    const auto harmonic = frame.image.pixelColor(32, 32);
    if (!frame.error.isEmpty() || std::abs(harmonic.red() - 190) > 2 || std::abs(harmonic.green() - 127) > 2) return 1;
    auto rotated = std::make_shared<GaussianSceneData>();
    oval.covariance_a = {0.185F, 0.175F, 0, 0.185F}; rotated->points = {oval};
    frame = renderer->render(rotated, camera, true);
    if (!frame.error.isEmpty() || frame.image.pixelColor(38, 38).red() < 150 || frame.image.pixelColor(38, 26).red() > 10) return 1;
    const auto orbited = navigate_gaussian_camera(camera, 3, 20, 10, 0, 0, 0);
    const auto target = orbited.worldToCamera().map(QVector3D(0, 0, 3));
    if (std::abs(target.x()) > 1e-5F || std::abs(target.y()) > 1e-5F || std::abs(target.z() - 3) > 1e-5F) return 1;
    auto negative_sh = std::make_shared<GaussianSceneData>();
    negative_sh->points = {point(2, {-1, 0, 0}), point(4, {1, 0, 0})};
    frame = renderer->render(negative_sh, camera, true);
    if (!frame.error.isEmpty() || std::abs(frame.image.pixelColor(32, 32).red() - 64) > 2) {
      std::cerr << "Negative SH must clamp before compositing, not after"; return 1;
    }
    auto over_white = std::make_shared<GaussianSceneData>();
    auto bright = point(3, {2, 0, 0}); bright.center_opacity[3] = 0.25F; over_white->points = {bright};
    frame = renderer->render(over_white, camera, true);
    if (!frame.error.isEmpty() || std::abs(frame.image.pixelColor(32, 32).red() - 128) > 2) return 1;
    std::cout << "Native Metal ordering, alpha/termination, rotated anisotropy, SH color, clipping and orbit tests pass\n";
    return 0;
  }
  if (args.size() < 4) return 1;
  const auto validation = validate_gaussian_ply(args[1], {});
  const auto loaded = load_gaussian_scene(args[1], args[2], validation.value("artifactSha256").toString());
  if (!loaded.scene) { std::cerr << loaded.error.toStdString(); return 1; }
  auto camera = loaded.scene->cameras.front();
  if (args.size() > 4) {
    const auto found = std::find_if(loaded.scene->cameras.begin(), loaded.scene->cameras.end(), [&](const auto& value) { return value.name == args[4]; });
    if (found == loaded.scene->cameras.end()) return 1;
    camera = *found;
  }
  auto frame = renderer->render(loaded.scene, camera, true);
  if (!frame.error.isEmpty()) { std::cerr << frame.error.toStdString(); return 1; }
  QFile output(args[3]);
  if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !frame.image.save(&output, "PNG")) return 1;
  QVariantMap proof = frame.metrics;
  proof.insert("camera", camera.description());
  proof.insert("artifactSha256", loaded.scene->artifact_sha256);
  if (args.size() > 5) {
    QImage reference(args[5]);
    if (reference.size() != frame.image.size()) return 1;
    double squared_error = 0;
    double absolute_error = 0;
    std::array<quint64, 256> error_histogram{};
    int max_error = 0;
    quint64 pixels_over_one = 0, pixels_over_two = 0;
    for (int y = 0; y < reference.height(); ++y) for (int x = 0; x < reference.width(); ++x) {
      const auto a = reference.pixelColor(x, y), b = frame.image.pixelColor(x, y);
      int pixel_error = 0;
      for (const auto delta : {a.red() - b.red(), a.green() - b.green(), a.blue() - b.blue()}) {
        squared_error += delta * delta; absolute_error += std::abs(delta);
        ++error_histogram[static_cast<size_t>(std::abs(delta))];
        pixel_error = std::max(pixel_error, std::abs(delta));
      }
      max_error = std::max(max_error, pixel_error);
      pixels_over_one += pixel_error > 1; pixels_over_two += pixel_error > 2;
    }
    const double mse = squared_error / (reference.width() * reference.height() * 3.0);
    proof.insert("referencePsnr", 10 * std::log10(255.0 * 255.0 / std::max(mse, 1e-12)));
    proof.insert("referenceMaxChannelError", max_error);
    proof.insert("referenceMeanAbsoluteChannelError", absolute_error / (reference.width() * reference.height() * 3.0));
    proof.insert("referencePixelsOverOne", pixels_over_one); proof.insert("referencePixelsOverTwo", pixels_over_two);
    quint64 cumulative = 0;
    for (size_t error = 0; error < error_histogram.size(); ++error) {
      cumulative += error_histogram[error];
      if (static_cast<double>(cumulative) >= reference.width() * reference.height() * 3.0 * 0.99) {
        proof.insert("referenceP99ChannelError", static_cast<int>(error)); break;
      }
    }
    if (args.contains("--scalar-audit")) proof.insert("scalarAudit", gaussian_reference::audit(*loaded.scene, camera, frame.image, reference));
  }
  std::vector<double> times;
  const int motion_frames = args.contains("--no-motion") ? 0 : 30;
  for (int i = 0; i < motion_frames; ++i) {
    const auto moving_camera = navigate_gaussian_camera(camera, loaded.scene->focus_distance, static_cast<float>(i) * 0.1F, 0, 0, 0, 0);
    const auto moving_frame = renderer->render(loaded.scene, moving_camera, false);
    if (!moving_frame.error.isEmpty() || !moving_frame.image.isNull() || !moving_frame.native_texture) return 1;
    times.push_back(moving_frame.metrics.value("renderMs").toDouble());
  }
  std::sort(times.begin(), times.end());
  proof.insert("cameraMotionFrames", motion_frames);
  if (!times.empty()) {
    proof.insert("motionMedianRenderMs", times[15]); proof.insert("motionP95RenderMs", times[28]); proof.insert("motionMaxRenderMs", times.back());
  }
  std::cout << QJsonDocument::fromVariant(proof).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
