#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QVariantMap>
#include <cmath>
#include <atomic>
#include <memory>

namespace photo_editor::desktop {
struct GaussianTimedCapture {
  QStringList sources;
  QVariantList cameras;
  QVariantList observations;
  QString manifest_sha256;
  QString error;
  int time_count = 0;
  double duration_seconds = 0;
  [[nodiscard]] bool valid() const { return error.isEmpty() && !sources.isEmpty(); }
};

// Explicit calibrated, synchronized, undistorted pinhole input. Times are
// seconds; camera matrices are row-major world-to-camera, in metres, with
// camera axes x right/y down/z forward and pixel coordinates from the top-left
// image corner. Calibration is supplied, not estimated or certified here.
inline GaussianTimedCapture validate_gaussian_timed_capture(const QVariantMap& root, const QString& source_root,
    const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  GaussianTimedCapture result;
  const auto fail = [&](const QString& reason) { result.error = reason; return result; };
  const auto stopped = [&] { return canceled && canceled->load(); };
  if (stopped()) return fail("Timed capture validation canceled.");
  if (root.value("schema").toString() != "framehouse-timed-capture-v1" ||
      root.value("timeUnit").toString() != "seconds" || root.value("worldUnit").toString() != "metres" ||
      root.value("cameraConvention").toString() != "opencv-world-to-camera" ||
      root.value("pixelConvention").toString() != "top-left-corner")
    return fail("Timed capture requires the v1 schema and explicit seconds/metres/camera/pixel conventions.");
  const auto cameras = root.value("cameras").toList();
  const auto times = root.value("frames").toList();
  if (cameras.size() < 2 || cameras.size() > 64 || times.size() < 2 || times.size() > 10000 ||
      cameras.size() * times.size() > 20000)
    return fail("Use 2–64 calibrated cameras, at least two times, and at most 20000 observations.");
  const auto number = [](const QVariant& value) {
    const auto type = value.metaType().id();
    return (type == QMetaType::Double || type == QMetaType::LongLong || type == QMetaType::ULongLong ||
            type == QMetaType::Int || type == QMetaType::UInt) && std::isfinite(value.toDouble());
  };
  QSet<QString> camera_ids;
  for (const auto& entry : cameras) {
    if (stopped()) return fail("Timed capture validation canceled.");
    const auto camera = entry.toMap();
    const auto id = camera.value("id").toString();
    if (id.isEmpty() || id.size() > 128 || camera_ids.contains(id) || camera.value("model").toString() != "PINHOLE" ||
        camera.value("undistorted").metaType().id() != QMetaType::Bool || !camera.value("undistorted").toBool())
      return fail("Each camera needs a unique ID and an explicitly undistorted PINHOLE calibration.");
    for (const auto& key : {"width", "height", "fx", "fy", "cx", "cy"})
      if (!number(camera.value(key))) return fail("Camera dimensions and intrinsics must be finite JSON numbers.");
    const auto width = camera.value("width").toDouble(), height = camera.value("height").toDouble();
    if (width < 1 || height < 1 || width > 100000 || height > 100000 || std::floor(width) != width || std::floor(height) != height ||
        camera.value("fx").toDouble() <= 0 || camera.value("fy").toDouble() <= 0 ||
        camera.value("fx").toDouble() > 1e8 || camera.value("fy").toDouble() > 1e8 ||
        std::abs(camera.value("cx").toDouble()) > 2 * width || std::abs(camera.value("cy").toDouble()) > 2 * height)
      return fail("Invalid calibrated image dimensions or focal/principal-point values.");
    const auto matrix = camera.value("worldToCamera").toList();
    if (matrix.size() != 16) return fail("Each worldToCamera matrix must have 16 row-major numbers.");
    for (const auto& value : matrix)
      if (!number(value) || std::abs(value.toDouble()) > 1e9) return fail("Invalid camera matrix value.");
    const auto m = [&](int row, int col) { return matrix[row * 4 + col].toDouble(); };
    for (int col = 0; col < 4; ++col)
      if (std::abs(m(3, col) - (col == 3 ? 1.0 : 0.0)) > 1e-8) return fail("Camera matrix must be affine.");
    for (int row = 0; row < 3; ++row) for (int other = 0; other < 3; ++other) {
      double dot = 0;
      for (int col = 0; col < 3; ++col) dot += m(row, col) * m(other, col);
      if (std::abs(dot - (row == other ? 1.0 : 0.0)) > 1e-4) return fail("Camera rotation must be orthonormal.");
    }
    const auto determinant = m(0,0)*(m(1,1)*m(2,2)-m(1,2)*m(2,1)) -
        m(0,1)*(m(1,0)*m(2,2)-m(1,2)*m(2,0)) + m(0,2)*(m(1,0)*m(2,1)-m(1,1)*m(2,0));
    if (std::abs(determinant - 1) > 1e-4) return fail("Camera rotation must be right-handed.");
    camera_ids.insert(id);
    result.cameras.append(camera);
  }
  QSet<QString> files;
  double previous = -1;
  double first = 0;
  for (qsizetype index = 0; index < times.size(); ++index) {
    const auto frame = times[index].toMap();
    const auto timestamp = frame.value("timeSeconds");
    if (!number(timestamp) || timestamp.toDouble() < 0 || timestamp.toDouble() > 86400 || timestamp.toDouble() <= previous)
      return fail("Frame times must be finite, nonnegative, strictly increasing seconds within one day.");
    previous = timestamp.toDouble();
    if (index == 0) first = previous;
    const auto images = frame.value("images").toList();
    if (images.size() != cameras.size()) return fail("Every time must contain exactly one image from every calibrated camera.");
    QSet<QString> seen;
    for (const auto& image : images) {
      if (stopped()) return fail("Timed capture validation canceled.");
      const auto observation = image.toMap();
      const auto id = observation.value("cameraId").toString();
      const auto relative = observation.value("file").toString();
      if (!camera_ids.contains(id) || seen.contains(id)) return fail("Unknown or repeated camera at a capture time.");
      if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\') ||
          relative.contains(QChar(0)) || relative.split('/').contains("..") || QDir::cleanPath(relative) != relative)
        return fail("Capture image paths must be clean relative paths inside the manifest folder.");
      const QFileInfo source(QDir(source_root).filePath(relative));
      const auto canonical = source.canonicalFilePath();
      if (!source.isFile() || !source.isReadable() || source.isSymLink() ||
          !canonical.startsWith(source_root + '/') || canonical != source.absoluteFilePath() || files.contains(canonical))
        return fail("Missing, repeated, linked or out-of-folder capture image.");
      seen.insert(id); files.insert(canonical); result.sources.append(canonical);
      result.observations.append(QVariantMap{{"cameraId", id}, {"timeSeconds", previous}, {"timeIndex", index}});
    }
  }
  result.time_count = static_cast<int>(times.size());
  result.duration_seconds = previous - first;
  return result;
}

inline GaussianTimedCapture read_gaussian_timed_capture(const QString& manifest_path,
    const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  GaussianTimedCapture result;
  if (canceled && canceled->load()) { result.error = "Timed capture validation canceled."; return result; }
  QFile file(manifest_path);
  if (QFileInfo(manifest_path).isSymLink() || !file.open(QIODevice::ReadOnly) ||
      file.size() <= 0 || file.size() > 16 * 1024 * 1024) {
    result.error = "Cannot read the timed-capture manifest (maximum 16 MiB; no symlink)."; return result;
  }
  const auto bytes = file.readAll();
  QJsonParseError parse;
  const auto document = QJsonDocument::fromJson(bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = "Invalid timed-capture JSON."; return result;
  }
  result = validate_gaussian_timed_capture(document.toVariant().toMap(), QFileInfo(manifest_path).canonicalPath(), canceled);
  result.manifest_sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  return result;
}
}  // namespace photo_editor::desktop
