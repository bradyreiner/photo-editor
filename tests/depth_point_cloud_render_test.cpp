#include "depth_point_cloud_viewport.h"
#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
#include <limits>
using namespace photo_editor::desktop;
namespace {
void write_frame(const QString& path, const std::vector<DepthCloudPoint>& points) {
  QByteArray data;
  for (const auto& p : points) {
    for (float value : {p.position.x(), p.position.y(), p.position.z()}) {
      char bytes[4]; qToLittleEndian(std::bit_cast<quint32>(value), bytes); data.append(bytes, 4);
    }
    data.append(char(qRed(p.color))); data.append(char(qGreen(p.color))); data.append(char(qBlue(p.color)));
  }
  QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) std::abort();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv); QTemporaryDir directory;
  write_frame(directory.filePath("000000.xyzrgb"), {{{0, 0, .7F}, qRgb(255, 0, 0)}, {{0, 0, 1.3F}, qRgb(0, 0, 255)}});
  DepthCloudView view; view.point_size = 3;
  const auto front = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view);
  if (!front.error.isEmpty() || front.image.pixelColor(100, 80) != QColor(Qt::red)) return fail("Front camera must depth-test the nearer red point");
  view.rotation = QQuaternion::fromAxisAndAngle({0, 1, 0}, 180);
  const auto back = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  if (!back.error.isEmpty() || back.image.pixelColor(100, 80) != QColor(Qt::blue)) return fail("Orbiting behind the same XYZ cloud must reverse foreground occlusion");
  write_frame(directory.filePath("000001.xyzrgb"), {{{-.12F, 0, .85F}, qRgb(255, 0, 0)}, {{.12F, 0, 1.15F}, qRgb(0, 0, 255)}});
  view.rotation = QQuaternion::fromAxisAndAngle({0, 1, 0}, 45);
  const auto normal = render_depth_cloud(directory.path(), 1, 2, {200, 160}, view);
  view.z_scale = 3;
  const auto exaggerated = render_depth_cloud(directory.path(), 1, 2, {200, 160}, view, normal.cloud);
  view.z_scale = 0;
  const auto flattened = render_depth_cloud(directory.path(), 1, 2, {200, 160}, view, normal.cloud);
  if (normal.image == exaggerated.image || normal.image == flattened.image || exaggerated.image == flattened.image)
    return fail("Actual Z stretching and flattening must change the projected geometry at a fixed orbit camera");
  if (normal.cloud != exaggerated.cloud || normal.cloud != flattened.cloud)
    return fail("Live Z edits reuse the immutable frame instead of rerunning or shifting depth images");
  view.rotation = {}; view.z_scale = 1; view.point_size = 8; view.shape = 1;
  const auto square = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  view.shape = 0;
  const auto round = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  view.shape = 2;
  const auto soft = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  if (square.image == round.image || round.image == soft.image || square.image == soft.image)
    return fail("Square, round and soft point looks must produce distinct pixels from the same XYZ frame");
  view.shape = 1; view.color_mode = 1; view.brightness = .5F; view.background = qRgb(255, 255, 255);
  const auto monochrome = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  const auto gray = monochrome.image.pixelColor(100, 80);
  if (gray.red() != gray.green() || gray.red() != gray.blue() || gray.red() >= 100 || monochrome.image.pixelColor(0, 0) != QColor(Qt::white))
    return fail("Monochrome, brightness and background controls must affect rendered points and empty space");
  view.color_mode = 2; view.brightness = 1;
  view.gradient_near = view.gradient_mid = view.gradient_far = qRgb(255, 0, 128);
  const auto pink = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  view.gradient_near = view.gradient_mid = view.gradient_far = qRgb(0, 220, 255);
  const auto cyan = render_depth_cloud(directory.path(), 0, 2, {200, 160}, view, front.cloud);
  if (pink.image.pixelColor(100, 80) != QColor(255, 0, 128) || cyan.image.pixelColor(100, 80) != QColor(0, 220, 255) || pink.cloud != cyan.cloud)
    return fail("Custom gradient stops must change actual point colors without changing geometry");
  if (render_depth_cloud(directory.path(), 1, 3, {200, 160}, view).error.isEmpty()) return fail("Truncated frames must be rejected");
  write_frame(directory.filePath("000002.xyzrgb"), {{{0, 0, std::numeric_limits<float>::quiet_NaN()}, qRgb(0, 255, 0)}});
  if (render_depth_cloud(directory.path(), 2, 1, {200, 160}, view).error.isEmpty()) return fail("Nonfinite depth must be rejected");
  std::cout << "Real XYZ front/back occlusion, live Z deformation, immutable cache and invalid frame checks passed\n";
}
