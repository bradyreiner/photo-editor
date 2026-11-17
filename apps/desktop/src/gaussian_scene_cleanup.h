#pragma once
#include "gaussian_lens_projection.h"
#include "gaussian_scene_data.h"
#include "gaussian_film_publication.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRectF>
#include <QPolygonF>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop {
// One byte per original vertex. History retains masks, never whole scene copies.
struct GaussianCleanupResult {
  std::shared_ptr<const GaussianSceneData> scene;
  QVariantMap result;
};
inline std::shared_ptr<const GaussianSceneData> gaussian_cleanup_source(
    const std::shared_ptr<const GaussianSceneData>& scene) {
  return scene->cleanup_source ? scene->cleanup_source : scene;
}
inline QByteArray gaussian_cleanup_mask(const GaussianSceneData& scene) {
  return scene.cleanup_mask.isEmpty() ? QByteArray(static_cast<qsizetype>(scene.points.size()), '\1') : scene.cleanup_mask;
}
inline QVariantMap gaussian_cleanup_receipt(const GaussianSceneData& original, const QByteArray& mask) {
  return {{"schema", "looksy-gaussian-cleanup-v1"}, {"artifactSha256", original.artifact_sha256},
      {"originalCount", static_cast<qulonglong>(original.points.size())},
      {"visibleCount", mask.count('\1')},
      {"maskSha256", QString::fromLatin1(QCryptographicHash::hash(mask, QCryptographicHash::Sha256).toHex())},
      {"visibility", QString::fromLatin1(mask.toBase64())}};
}
inline bool valid_gaussian_cleanup_mask(const GaussianSceneData& original, const QByteArray& mask) {
  return original.temporal.empty() && (original.spatial_factors.empty() || original.spatial_factors.size() == original.points.size()) && mask.size() == static_cast<qsizetype>(original.points.size()) &&
      mask.count('\1') > 0 && mask.count('\0') + mask.count('\1') == mask.size();
}
inline GaussianCleanupResult apply_gaussian_cleanup(const std::shared_ptr<const GaussianSceneData>& original,
    const QByteArray& mask, const QString& project, const std::shared_ptr<std::atomic_bool>& canceled, bool persist = true) {
  const auto fail = [](const QString& error) { return GaussianCleanupResult{{}, {{"ok", false}, {"error", error}}}; };
  if (!original || !valid_gaussian_cleanup_mask(*original, mask)) return fail("Cleanup must keep at least one splat in a static scene.");
  if (canceled && canceled->load()) return fail("Scene cleanup canceled.");
  auto scene = std::make_shared<GaussianSceneData>();
  scene->artifact_sha256 = original->artifact_sha256;
  scene->cameras = original->cameras; scene->sh_degree = original->sh_degree;
  scene->focus_distance = original->focus_distance;
  scene->cleanup_source = original; scene->cleanup_mask = mask;
  scene->cleanup_receipt = gaussian_cleanup_receipt(*original, mask);
  scene->points.reserve(static_cast<size_t>(mask.count('\1')));
  if (!original->spatial_factors.empty()) scene->spatial_factors.reserve(scene->points.capacity());
  for (qsizetype i = 0; i < mask.size(); ++i) {
    if (i % 4096 == 0 && canceled && canceled->load()) return fail("Scene cleanup canceled.");
    if (mask[i]) {
      scene->points.push_back(original->points[static_cast<size_t>(i)]);
      if (!original->spatial_factors.empty()) scene->spatial_factors.push_back(original->spatial_factors[static_cast<size_t>(i)]);
    }
  }
  if (persist) {
    if (!QFileInfo(project).isDir()) return fail("The scene project is unavailable; cleanup was not applied.");
    const auto path = QDir(project).filePath("cleanup.json");
    if (QFileInfo(path).isSymLink()) return fail("Scene cleanup must remain inside its project.");
    QSaveFile file(path);
    const auto bytes = QJsonDocument::fromVariant(scene->cleanup_receipt).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        (canceled && canceled->load()) || !file.commit()) return fail("Cannot save scene cleanup; the previous edit is unchanged.");
  }
  return {scene, {{"ok", true}, {"visibleCount", mask.count('\1')}}};
}
inline GaussianSceneLoad restore_gaussian_cleanup(GaussianSceneLoad loaded, const QString& project,
    const std::shared_ptr<std::atomic_bool>& canceled) {
  if (!loaded.scene) return loaded;
  QFile file(QDir(project).filePath("cleanup.json"));
  if (QFileInfo(file.fileName()).isSymLink()) return {{}, "Scene cleanup must remain inside its project."};
  if (!file.exists()) return loaded;
  if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
    return {{}, "Cannot read scene cleanup. The original artifact is unchanged."};
  const auto saved = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
  const auto encoded = saved.value("visibility").toString().toLatin1();
  const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || !valid_gaussian_cleanup_mask(*loaded.scene, decoded.decoded) ||
      saved != gaussian_cleanup_receipt(*loaded.scene, decoded.decoded))
    return {{}, "Scene cleanup is invalid or belongs to a different artifact. The original artifact is unchanged."};
  const auto edited = apply_gaussian_cleanup(loaded.scene, decoded.decoded, project, canceled, false);
  return {edited.scene, edited.result.value("error").toString()};
}
// Camera-forward distance in reconstruction units; independent of optical blur.
struct GaussianCleanupDepthRange {
  double near_depth = 0;
  double far_depth = std::numeric_limits<double>::infinity();
  bool valid() const {
    return std::isfinite(near_depth) && near_depth >= 0 && far_depth > near_depth &&
        (std::isfinite(far_depth) || far_depth == std::numeric_limits<double>::infinity());
  }
  bool contains(double depth) const { return depth > 0 && depth >= near_depth && depth <= far_depth; }
};
// Selection uses projected centers in the chosen depth range, never optical blur.
inline QByteArray select_gaussian_cleanup(const GaussianSceneData& original, QByteArray mask,
    const GaussianCamera& camera, const QRectF& rectangle, bool keep_inside,
    const std::shared_ptr<std::atomic_bool>& canceled = {}, const GaussianCleanupDepthRange& depth = {}) {
  if (!depth.valid() || mask.size() != static_cast<qsizetype>(original.points.size())) return {};
  const auto rect = rectangle.normalized();
  for (qsizetype i = 0; i < mask.size(); ++i) {
    if (i % 4096 == 0 && canceled && canceled->load()) return {};
    if (!mask[i]) continue;
    const auto& p = original.points[static_cast<size_t>(i)].center_opacity;
    const auto relative = QVector3D(p[0], p[1], p[2]) - camera.position;
    const double z = QVector3D::dotProduct(relative, camera.forward);
    bool inside = false;
    if (depth.contains(z)) {
      const auto pixel = gaussian_lens_display_pixel(camera,QPointF(
          camera.fx*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.right)/z+camera.cx,
          camera.fy*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.down)/z+camera.cy));
      const double x = pixel.x()/camera.image_size.width(), y = pixel.y()/camera.image_size.height();
      inside = rect.contains(QPointF(x, y));
    }
    if (inside != keep_inside) mask[i] = '\0';
  }
  return mask;
}
inline bool valid_gaussian_cleanup_polygon(const QPolygonF& polygon) {
  if (polygon.size() < 3 || polygon.size() > 512) return false;
  for (const auto& p : polygon)
    if (!std::isfinite(p.x()) || !std::isfinite(p.y()) || p.x() < 0 || p.x() > 1 || p.y() < 0 || p.y() > 1) return false;
  // A self-crossing outline can have zero signed area but a nonempty even-odd
  // fill. Reject collinear gestures without rejecting those valid outlines.
  const auto first = polygon.front();
  QPointF edge;
  for (const auto& p : polygon) if (std::hypot(p.x()-first.x(), p.y()-first.y()) > 1e-12) { edge = p-first; break; }
  for (const auto& p : polygon)
    if (std::abs(edge.x()*(p.y()-first.y())-edge.y()*(p.x()-first.x())) > 1e-12) return true;
  return false;
}
inline bool gaussian_cleanup_polygon_contains(const QPolygonF& polygon, const QPointF& point) {
  // Include the outline, consistently with rectangle cleanup. Even-odd fill
  // otherwise matches the interactive lasso preview, including crossings.
  for (qsizetype i = 0; i < polygon.size(); ++i) {
    const auto a = polygon[i], b = polygon[(i+1) % polygon.size()];
    const auto edge = b-a, offset = point-a;
    if (std::abs(edge.x()*offset.y()-edge.y()*offset.x()) <= 1e-12 &&
        point.x() >= std::min(a.x(),b.x())-1e-12 && point.x() <= std::max(a.x(),b.x())+1e-12 &&
        point.y() >= std::min(a.y(),b.y())-1e-12 && point.y() <= std::max(a.y(),b.y())+1e-12) return true;
  }
  return polygon.containsPoint(point, Qt::OddEvenFill);
}
inline QByteArray select_gaussian_cleanup_polygon(const GaussianSceneData& original, QByteArray mask,
    const GaussianCamera& camera, const QPolygonF& polygon, bool keep_inside,
    const std::shared_ptr<std::atomic_bool>& canceled = {}, const GaussianCleanupDepthRange& depth = {}) {
  if (!depth.valid() || !valid_gaussian_cleanup_polygon(polygon) || mask.size() != static_cast<qsizetype>(original.points.size())) return {};
  const auto bounds = polygon.boundingRect().adjusted(-1e-12,-1e-12,1e-12,1e-12);
  for (qsizetype i = 0; i < mask.size(); ++i) {
    if (i % 4096 == 0 && canceled && canceled->load()) return {};
    if (!mask[i]) continue;
    const auto& p = original.points[static_cast<size_t>(i)].center_opacity;
    const auto relative = QVector3D(p[0],p[1],p[2])-camera.position;
    const double z = QVector3D::dotProduct(relative,camera.forward);
    bool inside = false;
    if (depth.contains(z)) {
      const auto pixel = gaussian_lens_display_pixel(camera,QPointF(
          camera.fx*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.right)/z+camera.cx,
          camera.fy*camera.photo.focal_scale*QVector3D::dotProduct(relative,camera.down)/z+camera.cy));
      const QPointF normalized(pixel.x()/camera.image_size.width(),pixel.y()/camera.image_size.height());
      inside = bounds.contains(normalized) && gaussian_cleanup_polygon_contains(polygon,normalized);
    }
    if (inside != keep_inside) mask[i] = '\0';
  }
  return mask;
}
inline QByteArray filter_gaussian_cleanup(const GaussianSceneData& original, QByteArray mask,
    double minimum_opacity, double maximum_sigma, const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  for (qsizetype i = 0; i < mask.size(); ++i) {
    if (i % 4096 == 0 && canceled && canceled->load()) return {};
    if (!mask[i]) continue;
    const auto& p = original.points[static_cast<size_t>(i)];
    // RMS radius is rotation invariant: sqrt(trace(covariance)/3).
    const double sigma = std::sqrt((double(p.covariance_a[0]) + p.covariance_a[3] + p.covariance_b[1]) / 3);
    if (p.center_opacity[3] < minimum_opacity || (maximum_sigma > 0 && sigma > maximum_sigma)) mask[i] = '\0';
  }
  return mask;
}
inline QVariantMap export_gaussian_cleanup(const QString& artifact, const QString& destination,
    const GaussianSceneData& original, const QByteArray& mask, const std::shared_ptr<std::atomic_bool>& canceled,
    const QVariantMap& current_camera = {}, GaussianAlphaModel alpha_model = GaussianAlphaModel::Brush999) {
  const auto fail = [](const QString& error) { return QVariantMap{{"ok", false}, {"error", error}}; };
  if (!valid_gaussian_alpha_model(alpha_model) || !valid_gaussian_cleanup_mask(original, mask) || !QFileInfo(destination).isAbsolute() ||
      QFileInfo(destination).suffix().compare("ply", Qt::CaseInsensitive) != 0 ||
      QFileInfo::exists(destination) || QFileInfo::exists(destination + ".splat.json"))
    return fail("Choose a new PLY destination for the cleaned static scene.");
  QFile input(artifact);
  if (!input.open(QIODevice::ReadOnly)) return fail("Cannot read the original Gaussian artifact.");
  QByteArray header;
  while (header.size() < 16384 && !header.endsWith("end_header\n") && !header.endsWith("end_header\r\n")) {
    const auto line = input.readLine(16384 - header.size());
    if (line.isEmpty()) return fail("The Gaussian header is incomplete.");
    header += line;
  }
  auto lines = header.split('\n');
  qsizetype row_bytes = 0;
  bool vertex = false;
  for (auto& line : lines) {
    if (line.startsWith("property ")) row_bytes += 4;
    if (line.startsWith("element vertex ")) {
      if (vertex || line.mid(15).trimmed().toLongLong() != mask.size()) return fail("The original vertex count changed.");
      vertex = true; line = "element vertex " + QByteArray::number(mask.count('\1'));
    }
  }
  if (!vertex || row_bytes < 56 || row_bytes > 512 || input.size() != header.size() + mask.size() * row_bytes)
    return fail("The original Gaussian layout changed.");
  QTemporaryDir staging(QDir(QFileInfo(destination).absolutePath()).filePath(".looksy-splat-XXXXXX"));
  if (!staging.isValid()) return fail("Cannot stage the cleaned scene beside its destination.");
  QFile output(staging.filePath("scene.ply")), receipt(staging.filePath("scene.json"));
  const auto cleaned_header = lines.join('\n');
  if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(cleaned_header) != cleaned_header.size())
    return fail("Cannot write the cleaned Gaussian header.");
  QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(header);
  QCryptographicHash output_hash(QCryptographicHash::Sha256); output_hash.addData(cleaned_header);
  for (qsizetype first = 0; first < mask.size(); first += 4096) {
    if (canceled && canceled->load()) return fail("Splat export canceled.");
    const auto rows = std::min(qsizetype{4096}, mask.size() - first);
    const auto bytes = input.read(rows * row_bytes);
    if (bytes.size() != rows * row_bytes) return fail("The original Gaussian payload changed.");
    hash.addData(bytes);
    QByteArray selected; selected.reserve(bytes.size());
    for (qsizetype row = 0; row < rows; ++row) if (mask[first + row]) selected.append(bytes.constData() + row * row_bytes, row_bytes);
    if (output.write(selected) != selected.size()) return fail("Cannot write the cleaned Gaussian payload.");
    output_hash.addData(selected);
  }
  if (!input.atEnd() || QString::fromLatin1(hash.result().toHex()) != original.artifact_sha256)
    return fail("The original artifact changed; no cleaned scene was published.");
  auto metadata = gaussian_cleanup_receipt(original, mask);
  metadata.insert("exportedArtifactSha256", QString::fromLatin1(output_hash.result().toHex()));
  metadata.insert("alphaModel",gaussian_alpha_model_name(alpha_model));
  if (!current_camera.isEmpty()) metadata.insert("currentCamera", current_camera);
  metadata.insert("cameraConvention", "COLMAP-right-down-forward");
  QVariantList cameras; for (const auto& camera : original.cameras) cameras.append(camera.description());
  metadata.insert("captureCameras", cameras);
  const auto bytes = QJsonDocument::fromVariant(metadata).toJson();
  if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly) || receipt.write(bytes) != bytes.size() ||
      !output.flush() || !receipt.flush() || (canceled && canceled->load())) return fail("Cannot finish the cleaned scene export.");
  output.close(); receipt.close();
  const auto publication = publish_gaussian_media_files(output.fileName(), receipt.fileName(), destination, ".splat.json");
  if (!publication.ok) return fail(publication.error);
  return {{"ok", true}, {"outputPath", destination}, {"visibleCount", mask.count('\1')}};
}
}  // namespace photo_editor::desktop
