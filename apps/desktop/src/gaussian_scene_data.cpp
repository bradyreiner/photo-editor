#include "gaussian_scene_data.h"
#include "gaussian_ply_validation.h"
#include <QDataStream>
#include <QDir>
#include <QHash>
#include <QQuaternion>
#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
QMatrix4x4 GaussianCamera::worldToCamera() const {
  QMatrix4x4 matrix;
  matrix.setRow(0, QVector4D(right, -QVector3D::dotProduct(right, position)));
  matrix.setRow(1, QVector4D(down, -QVector3D::dotProduct(down, position)));
  matrix.setRow(2, QVector4D(forward, -QVector3D::dotProduct(forward, position)));
  return matrix;
}
GaussianCamera GaussianCamera::resized(QSize size) const {
  auto result = *this;
  const float sx = static_cast<float>(size.width()) / static_cast<float>(image_size.width());
  const float sy = static_cast<float>(size.height()) / static_cast<float>(image_size.height());
  result.fx *= sx; result.cx *= sx;
  result.fy *= sy; result.cy *= sy;
  result.image_size = size;
  return result;
}
QVariantMap GaussianCamera::description() const {
  const auto vector = [](QVector3D value) { return QVariantList{value.x(), value.y(), value.z()}; };
  QVariantMap result{{"name", name}, {"width", image_size.width()}, {"height", image_size.height()},
      {"fx", fx}, {"fy", fy}, {"cx", cx}, {"cy", cy}, {"position", vector(position)},
      {"right", vector(right)}, {"down", vector(down)}, {"forward", vector(forward)},
      {"convention", "COLMAP-right-down-forward"}};
  // Keep unedited capture descriptions byte-compatible with existing scene
  // fingerprints. Older saved shots have an implicit neutral photo setup.
  if (photo != GaussianPhotoSettings{}) result.insert("photo", photo.description());
  if (physical) result.insert("physicalCalibration",physical->description());
  return result;
}
GaussianCamera navigate_gaussian_camera(const GaussianCamera& base, float focus, float yaw,
    float pitch, float dolly, float pan_x, float pan_y) {
  auto camera = base;
  const auto rotation = QQuaternion::fromAxisAndAngle(-base.down, yaw) *
      QQuaternion::fromAxisAndAngle(base.right, pitch);
  const auto target = base.position + base.forward * focus + base.right * pan_x * focus + base.down * pan_y * focus;
  camera.position = target - rotation.rotatedVector(base.forward) * focus * std::exp(-dolly);
  camera.right = rotation.rotatedVector(base.right);
  camera.down = rotation.rotatedVector(base.down);
  camera.forward = rotation.rotatedVector(base.forward);
  return camera;
}

namespace {
bool finite(double value) { return std::isfinite(value) && std::abs(value) < 1e12; }
QString read_cameras(const QString& directory, std::vector<GaussianCamera>* result) {
  QFile cameras_file(QDir(directory).filePath("cameras.bin"));
  QFile images_file(QDir(directory).filePath("images.bin"));
  if (!cameras_file.open(QIODevice::ReadOnly) || !images_file.open(QIODevice::ReadOnly))
    return "The undistorted COLMAP camera files are missing.";
  QDataStream cameras(&cameras_file), images(&images_file);
  cameras.setByteOrder(QDataStream::LittleEndian); images.setByteOrder(QDataStream::LittleEndian);
  quint64 count = 0;
  cameras >> count;
  if (count == 0 || count > 2000) return "Invalid camera count.";
  QHash<quint32, GaussianCamera> intrinsics;
  for (quint64 i = 0; i < count; ++i) {
    quint32 id = 0; qint32 model = 0; quint64 width = 0, height = 0;
    cameras >> id >> model >> width >> height;
    if ((model != 0 && model != 1) || width == 0 || height == 0 || width > 16384 || height > 16384)
      return "Scene viewing requires undistorted pinhole cameras.";
    double fx = 0, fy = 0, cx = 0, cy = 0;
    cameras >> fx;
    if (model == 1) cameras >> fy; else fy = fx;
    cameras >> cx >> cy;
    if (!finite(fx) || !finite(fy) || !finite(cx) || !finite(cy) || fx <= 0 || fy <= 0 || intrinsics.contains(id))
      return "Invalid camera intrinsics.";
    GaussianCamera camera;
    camera.image_size = QSize(static_cast<int>(width), static_cast<int>(height));
    camera.fx = static_cast<float>(fx); camera.fy = static_cast<float>(fy);
    camera.cx = static_cast<float>(cx); camera.cy = static_cast<float>(cy);
    intrinsics.insert(id, camera);
  }
  if (cameras.status() != QDataStream::Ok || !cameras_file.atEnd()) return "Truncated or unsupported camera data.";
  images >> count;
  if (count == 0 || count > 2000) return "Invalid registered-view count.";
  for (quint64 i = 0; i < count; ++i) {
    quint32 image_id = 0, camera_id = 0;
    std::array<double, 7> pose{};
    images >> image_id;
    for (auto& value : pose) { images >> value; if (!finite(value)) return "Invalid camera pose."; }
    images >> camera_id;
    if (!intrinsics.contains(camera_id)) return "A view references an unknown camera.";
    QByteArray name;
    char ch = 0;
    do {
      if (images.readRawData(&ch, 1) != 1 || name.size() > 1024) return "Invalid camera name.";
      if (ch != 0) name.append(ch);
    } while (ch != 0);
    quint64 point_count = 0;
    images >> point_count;
    if (point_count > 1000000 || static_cast<qint64>(point_count * 24) > images_file.size() - images_file.pos() ||
        !images_file.seek(images_file.pos() + static_cast<qint64>(point_count * 24))) return "Truncated camera observations.";
    auto camera = intrinsics.value(camera_id);
    camera.name = QString::fromUtf8(name);
    QQuaternion quaternion(static_cast<float>(pose[0]), static_cast<float>(pose[1]),
        static_cast<float>(pose[2]), static_cast<float>(pose[3]));
    if (quaternion.lengthSquared() < 1e-12F) return "Invalid camera rotation.";
    quaternion.normalize();
    const auto inverse = quaternion.conjugated();
    camera.position = inverse.rotatedVector(-QVector3D(static_cast<float>(pose[4]), static_cast<float>(pose[5]), static_cast<float>(pose[6])));
    camera.right = inverse.rotatedVector({1, 0, 0});
    camera.down = inverse.rotatedVector({0, 1, 0});
    camera.forward = inverse.rotatedVector({0, 0, 1});
    result->push_back(camera);
  }
  if (images.status() != QDataStream::Ok || !images_file.atEnd()) return "Truncated or unsupported image poses.";
  std::sort(result->begin(), result->end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  return {};
}
}

GaussianSceneLoad load_gaussian_scene(const QString& artifact, const QString& camera_directory,
    const QString& expected_sha256, const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto validation = validate_gaussian_ply(artifact, canceled);
  if (!validation.value("ok").toBool()) return {{}, validation.value("error").toString()};
  if (expected_sha256.isEmpty() || validation.value("artifactSha256").toString() != expected_sha256)
    return {{}, "The artifact does not match the verified scene receipt."};
  const auto count = validation.value("vertexCount").toULongLong();
  if (count > 2000000) return {{}, "This viewport currently supports at most two million Gaussians."};
  auto scene = std::make_shared<GaussianSceneData>();
  scene->artifact_sha256 = expected_sha256;
  const auto error = read_cameras(camera_directory, &scene->cameras);
  if (!error.isEmpty()) return {{}, error};
  QFile file(artifact);
  if (!file.open(QIODevice::ReadOnly)) return {{}, "Cannot reopen the verified artifact."};
  QStringList properties;
  QByteArray header;
  while (header.size() < 16384) {
    const auto line = file.readLine(16384 - header.size());
    if (line.isEmpty()) return {{}, "The artifact changed while loading."};
    header += line;
    if (line.startsWith("property ")) properties.append(QString::fromLatin1(line.simplified().split(' ').last()));
    if (line.trimmed() == "end_header") break;
  }
  int rest_count = 0;
  for (const auto& property : properties) if (property.startsWith("f_rest_")) ++rest_count;
  if (rest_count != 0 && rest_count != 9 && rest_count != 24 && rest_count != 45)
    return {{}, "Only complete spherical-harmonic degrees zero through three are supported."};
  scene->sh_degree = rest_count == 45 ? 3 : rest_count == 24 ? 2 : rest_count == 9 ? 1 : 0;
  const auto index = [&](const QString& name) { return properties.indexOf(name); };
  for (int i = 0; i < rest_count; ++i) if (index(QStringLiteral("f_rest_%1").arg(i)) < 0)
    return {{}, "Spherical-harmonic coefficients are incomplete."};
  std::array<qsizetype, 14> required{};
  const QStringList names{"x", "y", "z", "opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3", "f_dc_0", "f_dc_1", "f_dc_2"};
  for (qsizetype i = 0; i < names.size(); ++i) {
    required[static_cast<size_t>(i)] = index(names[i]);
    if (required[static_cast<size_t>(i)] < 0) return {{}, "The Gaussian layout changed while loading."};
  }
  std::array<qsizetype, 45> rest_indices{};
  for (int i = 0; i < rest_count; ++i) rest_indices[static_cast<size_t>(i)] = index(QStringLiteral("f_rest_%1").arg(i));
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(header);
  scene->points.reserve(static_cast<size_t>(count));
  const auto row_bytes = properties.size() * 4;
  for (quint64 first = 0; first < count; first += 4096) {
    if (canceled && canceled->load()) return {{}, "Scene loading canceled."};
    const auto rows = qMin(quint64{4096}, count - first);
    const auto chunk = file.read(static_cast<qint64>(rows) * row_bytes);
    if (chunk.size() != static_cast<qint64>(rows) * row_bytes) return {{}, "The artifact changed while loading."};
    hash.addData(chunk);
    for (quint64 row = 0; row < rows; ++row) {
      const auto value = [&](qsizetype column) {
        return std::bit_cast<float>(qFromLittleEndian<quint32>(chunk.constData() + static_cast<qsizetype>(row) * row_bytes + column * 4));
      };
      GaussianPoint point;
      for (size_t axis = 0; axis < 3; ++axis) point.center_opacity[axis] = value(required[axis]);
      point.center_opacity[3] = 1.0F / (1.0F + std::exp(-value(required[3])));
      QQuaternion rotation(value(required[7]), value(required[8]), value(required[9]), value(required[10]));
      const auto matrix = rotation.normalized().toRotationMatrix();
      double covariance[3][3]{};
      for (size_t axis = 0; axis < 3; ++axis) {
        const double variance = std::exp(2.0 * static_cast<double>(value(required[4 + axis])));
        if (!finite(variance)) return {{}, "Gaussian scale exceeds the supported numeric range."};
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
          covariance[r][c] += variance * matrix(r, static_cast<int>(axis)) * matrix(c, static_cast<int>(axis));
      }
      point.covariance_a = {static_cast<float>(covariance[0][0]), static_cast<float>(covariance[0][1]),
          static_cast<float>(covariance[0][2]), static_cast<float>(covariance[1][1])};
      point.covariance_b = {static_cast<float>(covariance[1][2]), static_cast<float>(covariance[2][2]), 0, 0};
      for (size_t channel = 0; channel < 3; ++channel) {
        point.sh[channel] = value(required[11 + channel]);
        for (int basis = 0; basis < rest_count / 3; ++basis)
          point.sh[static_cast<size_t>((basis + 1) * 3) + channel] =
              value(rest_indices[channel * static_cast<size_t>(rest_count / 3) + static_cast<size_t>(basis)]);
      }
      scene->points.push_back(point);
    }
  }
  if (!file.atEnd() || QString::fromLatin1(hash.result().toHex()) != expected_sha256)
    return {{}, "The Gaussian bytes changed while loading."};
  std::vector<float> depths;
  const auto& camera = scene->cameras.front();
  for (const auto& point : scene->points) {
    const auto& p = point.center_opacity;
    const float depth = QVector3D::dotProduct(QVector3D(p[0], p[1], p[2]) - camera.position, camera.forward);
    if (depth > 0 && std::isfinite(depth)) depths.push_back(depth);
  }
  if (!depths.empty()) {
    auto middle = depths.begin() + static_cast<std::ptrdiff_t>(depths.size() / 2);
    std::nth_element(depths.begin(), middle, depths.end());
    scene->focus_distance = std::max(*middle, 0.01F);
  }
  return {scene, {}};
}
}  // namespace photo_editor::desktop
