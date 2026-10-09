#include "timeline_scene_runtime.h"

#include <QDir>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>
#include <QTemporaryDir>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace photo_editor::desktop {
namespace {

constexpr int kMaximumFrames = 900;
constexpr int kMaximumImageEdge = 960;
constexpr int kMaximumSparsePoints = 500000;
constexpr int kMaximumRetainedScenePoints = 2000;
constexpr int kMinimumScenePoints = 50;
constexpr int kMinimumFrameObservations = 20;
constexpr double kMaximumMeanReprojectionErrorPx = 3.0;
constexpr double kMinimumMedianTriangulationAngleDegrees = 1.0;
constexpr double kPi = 3.141592653589793238462643383279502884;

using Vec3 = std::array<double, 3>;

struct ParsedCamera final {
  quint64 id = 0;
  int width = 0;
  int height = 0;
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;
};

struct ParsedImageObservation final {
  double x = 0.0;
  double y = 0.0;
  QString point_id;
};

struct ParsedImage final {
  quint64 id = 0;
  int frame_index = -1;
  QString name;
  quint64 camera_id = 0;
  TimelineSceneFrame frame;
  QVector<ParsedImageObservation> observations;
};

struct ParseSceneResult final {
  bool ok = false;
  TimelineSceneResult scene;
  QString error;
};

TimelineSceneResult result_with_status(TimelineSceneStatus status, const QString& message) {
  TimelineSceneResult result;
  result.status = status;
  result.message = message;
  return result;
}

bool finite(double value) {
  return std::isfinite(value) && std::abs(value) < 1.0e15;
}

QStringList fields(const QByteArray& line) {
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  return QString::fromUtf8(line).trimmed().split(whitespace, Qt::SkipEmptyParts);
}

bool parse_unsigned(const QString& text, quint64* result) {
  bool ok = false;
  const quint64 value = text.toULongLong(&ok, 10);
  if (!ok || value == 0) {
    return false;
  }
  if (result != nullptr) {
    *result = value;
  }
  return true;
}

bool parse_double(const QString& text, double* result) {
  bool ok = false;
  const double value = text.toDouble(&ok);
  if (!ok || !finite(value)) {
    return false;
  }
  if (result != nullptr) {
    *result = value;
  }
  return true;
}

bool read_next_data_line(QFile& file, QByteArray* line) {
  while (!file.atEnd()) {
    const QByteArray current = file.readLine(8 * 1024 * 1024);
    if (current.isEmpty() || current.size() >= 8 * 1024 * 1024) {
      return false;
    }
    const QByteArray trimmed = current.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#')) {
      continue;
    }
    *line = current;
    return true;
  }
  return false;
}

std::array<double, 9> rotation_from_colmap_quaternion(
    double qw, double qx, double qy, double qz, bool* ok) {
  const double norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
  if (!finite(norm) || norm < 1.0e-10) {
    *ok = false;
    return {};
  }
  qw /= norm;
  qx /= norm;
  qy /= norm;
  qz /= norm;
  *ok = true;
  return {
      1.0 - 2.0 * (qy * qy + qz * qz), 2.0 * (qx * qy - qz * qw), 2.0 * (qx * qz + qy * qw),
      2.0 * (qx * qy + qz * qw), 1.0 - 2.0 * (qx * qx + qz * qz), 2.0 * (qy * qz - qx * qw),
      2.0 * (qx * qz - qy * qw), 2.0 * (qy * qz + qx * qw), 1.0 - 2.0 * (qx * qx + qy * qy),
  };
}

bool read_camera_model(const QString& path, ParsedCamera* camera, QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QStringLiteral("COLMAP did not export cameras.txt");
    return false;
  }
  int camera_count = 0;
  QByteArray line;
  while (read_next_data_line(file, &line)) {
    const QStringList token = fields(line);
    const bool simple_pinhole = token.size() == 7 && token.at(1) == QStringLiteral("SIMPLE_PINHOLE");
    const bool pinhole = token.size() == 8 && token.at(1) == QStringLiteral("PINHOLE");
    if (!simple_pinhole && !pinhole) {
      *error = QStringLiteral("Only one shared zero-distortion pinhole camera is supported for scene projection");
      return false;
    }
    quint64 id = 0;
    bool width_ok = false;
    bool height_ok = false;
    const int width = token.at(2).toInt(&width_ok);
    const int height = token.at(3).toInt(&height_ok);
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    const bool intrinsics_ok = simple_pinhole
                                   ? parse_double(token.at(4), &fx) &&
                                         parse_double(token.at(5), &cx) && parse_double(token.at(6), &cy)
                                   : parse_double(token.at(4), &fx) && parse_double(token.at(5), &fy) &&
                                         parse_double(token.at(6), &cx) && parse_double(token.at(7), &cy);
    if (simple_pinhole) {
      fy = fx;
    }
    if (!parse_unsigned(token.at(0), &id) || !width_ok || !height_ok ||
        width < 64 || height < 64 || width > kMaximumImageEdge || height > kMaximumImageEdge ||
        !intrinsics_ok ||
        fx <= 0.0 || fy <= 0.0 || cx < -width || cx > 2.0 * width || cy < -height || cy > 2.0 * height) {
      *error = QStringLiteral("COLMAP exported invalid PINHOLE camera intrinsics");
      return false;
    }
    ++camera_count;
    if (camera_count > 1) {
      *error = QStringLiteral("COLMAP exported multiple cameras; a single-camera scene is required");
      return false;
    }
    *camera = {id, width, height, fx, fy, cx, cy};
  }
  if (!file.atEnd() || camera_count != 1) {
    *error = QStringLiteral("COLMAP camera export is truncated or empty");
    return false;
  }
  return true;
}

bool parse_images(const QString& path,
                  const TimelineSceneSolveRequest& request,
                  const ParsedCamera& camera,
                  QVector<ParsedImage>* images,
                  QHash<quint64, int>* index_by_id,
                  QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    *error = QStringLiteral("COLMAP did not export images.txt");
    return false;
  }
  QVector<ParsedImage> parsed(request.source_times_us.size());
  QVector<bool> seen(request.source_times_us.size(), false);
  QHash<quint64, int> parsed_index_by_id;
  QByteArray metadata_line;
  while (read_next_data_line(file, &metadata_line)) {
    const QStringList token = fields(metadata_line);
    if (token.size() != 10) {
      *error = QStringLiteral("COLMAP image entry has fewer than ten fields");
      return false;
    }
    quint64 image_id = 0;
    quint64 camera_id = 0;
    std::array<double, 7> pose{};
    if (!parse_unsigned(token.at(0), &image_id)) {
      *error = QStringLiteral("COLMAP image ID is invalid");
      return false;
    }
    for (int index = 0; index < 7; ++index) {
      if (!parse_double(token.at(index + 1), &pose[static_cast<size_t>(index)])) {
        *error = QStringLiteral("COLMAP image pose contains an invalid number");
        return false;
      }
    }
    if (!parse_unsigned(token.at(8), &camera_id) || camera_id != camera.id) {
      *error = QStringLiteral("COLMAP image references an unsupported camera ID");
      return false;
    }
    const QString name = token.at(9);
    bool frame_name_ok = false;
    int frame_index = -1;
    if (name.size() == 10 && name.endsWith(QStringLiteral(".jpg"))) {
      const QString numeric = name.left(6);
      bool numeric_ok = false;
      const int candidate = numeric.toInt(&numeric_ok, 10);
      if (numeric_ok && numeric == QStringLiteral("%1").arg(candidate, 6, 10, QLatin1Char('0')) &&
          candidate >= 0 && candidate < parsed.size()) {
        frame_index = candidate;
        frame_name_ok = true;
      }
    }
    if (!frame_name_ok || seen.at(frame_index) || parsed_index_by_id.contains(image_id)) {
      *error = QStringLiteral("COLMAP image names must be a unique complete numeric JPEG sequence");
      return false;
    }
    if (file.atEnd()) {
      *error = QStringLiteral("COLMAP image observations are missing");
      return false;
    }
    const QByteArray observations_line = file.readLine(64 * 1024 * 1024);
    if (observations_line.isNull() || observations_line.size() >= 64 * 1024 * 1024) {
      *error = QStringLiteral("COLMAP image observations are truncated or oversized");
      return false;
    }
    const QStringList observation_tokens = fields(observations_line);
    if (observation_tokens.size() % 3 != 0 || observation_tokens.size() / 3 > 1000000) {
      *error = QStringLiteral("COLMAP image observations have an invalid length");
      return false;
    }
    ParsedImage image;
    image.id = image_id;
    image.frame_index = frame_index;
    image.name = name;
    image.camera_id = camera_id;
    image.frame.source_time_us = request.source_times_us.at(frame_index);
    image.frame.image_name = name;
    image.frame.fx = camera.fx;
    image.frame.fy = camera.fy;
    image.frame.cx = camera.cx;
    image.frame.cy = camera.cy;
    bool rotation_ok = false;
    const auto rotation = rotation_from_colmap_quaternion(
        pose[0], pose[1], pose[2], pose[3], &rotation_ok);
    if (!rotation_ok) {
      *error = QStringLiteral("COLMAP image rotation quaternion is degenerate");
      return false;
    }
    image.frame.world_to_camera = {
        rotation[0], rotation[1], rotation[2], pose[4],
        rotation[3], rotation[4], rotation[5], pose[5],
        rotation[6], rotation[7], rotation[8], pose[6],
    };
    image.observations.reserve(observation_tokens.size() / 3);
    for (qsizetype index = 0; index < observation_tokens.size(); index += 3) {
      ParsedImageObservation observation;
      if (!parse_double(observation_tokens.at(index), &observation.x) ||
          !parse_double(observation_tokens.at(index + 1), &observation.y) ||
          observation.x < 0.0 || observation.x > camera.width ||
          observation.y < 0.0 || observation.y > camera.height) {
        *error = QStringLiteral("COLMAP keypoint coordinates lie outside the source frame");
        return false;
      }
      const QString point_id = observation_tokens.at(index + 2);
      if (point_id != QStringLiteral("-1")) {
        quint64 numeric_point_id = 0;
        if (!parse_unsigned(point_id, &numeric_point_id)) {
          *error = QStringLiteral("COLMAP keypoint references an invalid 3D point ID");
          return false;
        }
        observation.point_id = QString::number(numeric_point_id);
      }
      image.observations.append(observation);
    }
    parsed[frame_index] = image;
    seen[frame_index] = true;
    parsed_index_by_id.insert(image_id, frame_index);
  }
  if (!file.atEnd() || parsed_index_by_id.size() != request.source_times_us.size()) {
    *error = QStringLiteral("COLMAP images.txt is truncated or does not register every source frame");
    return false;
  }
  for (int index = 0; index < seen.size(); ++index) {
    if (!seen.at(index)) {
      *error = QStringLiteral("COLMAP left a source frame unregistered");
      return false;
    }
  }
  *images = parsed;
  *index_by_id = parsed_index_by_id;
  return true;
}

double vector_dot(const Vec3& a, const Vec3& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 vector_subtract(const Vec3& a, const Vec3& b) {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double vector_length(const Vec3& value) {
  return std::sqrt(vector_dot(value, value));
}

Vec3 camera_center(const TimelineSceneFrame& frame) {
  const auto& m = frame.world_to_camera;
  const Vec3 t{m[3], m[7], m[11]};
  return {
      -(m[0] * t[0] + m[4] * t[1] + m[8] * t[2]),
      -(m[1] * t[0] + m[5] * t[1] + m[9] * t[2]),
      -(m[2] * t[0] + m[6] * t[1] + m[10] * t[2]),
  };
}

double median(QVector<double> values) {
  if (values.isEmpty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const qsizetype middle = values.size() / 2;
  if (values.size() % 2 != 0) {
    return values.at(middle);
  }
  return (values.at(middle - 1) + values.at(middle)) * 0.5;
}

bool parse_points(const QString& path,
                  QVector<ParsedImage>* images,
                  const QHash<quint64, int>& image_index_by_id,
                  TimelineSceneResult* scene,
                  QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 512LL * 1024 * 1024) {
    *error = QStringLiteral("COLMAP points3D.txt is missing or exceeds the supported size");
    return false;
  }
  QVector<double> point_errors;
  QVector<double> triangulation_angles;
  point_errors.reserve(kMaximumSparsePoints);
  triangulation_angles.reserve(kMaximumSparsePoints);
  QByteArray line;
  while (!file.atEnd()) {
    line = file.readLine(8 * 1024 * 1024);
    if (line.isEmpty() || line.size() >= 8 * 1024 * 1024) {
      *error = QStringLiteral("COLMAP 3D point line is truncated or oversized");
      return false;
    }
    const QStringList token = fields(line);
    if (token.isEmpty() || token.first().startsWith(QLatin1Char('#'))) {
      continue;
    }
    if (scene->points.size() >= kMaximumSparsePoints || token.size() < 10 ||
        (token.size() - 8) % 2 != 0) {
      *error = QStringLiteral("COLMAP sparse point list has an invalid or unsupported shape");
      return false;
    }
    quint64 point_id = 0;
    Vec3 point{};
    std::array<int, 3> rgb{};
    double reprojection_error = 0.0;
    if (!parse_unsigned(token.at(0), &point_id) ||
        !parse_double(token.at(1), &point[0]) || !parse_double(token.at(2), &point[1]) ||
        !parse_double(token.at(3), &point[2])) {
      *error = QStringLiteral("COLMAP sparse point has an invalid ID or coordinate");
      return false;
    }
    for (int channel = 0; channel < 3; ++channel) {
      bool ok = false;
      const int value = token.at(channel + 4).toInt(&ok);
      if (!ok || value < 0 || value > 255) {
        *error = QStringLiteral("COLMAP sparse point has invalid color values");
        return false;
      }
      rgb[static_cast<size_t>(channel)] = value;
    }
    if (!parse_double(token.at(7), &reprojection_error) || reprojection_error < 0.0) {
      *error = QStringLiteral("COLMAP sparse point has an invalid reprojection error");
      return false;
    }

    TimelineScenePoint scene_point;
    scene_point.id = QString::number(point_id);
    scene_point.world_position = point;
    scene_point.color_rgb = rgb;
    scene_point.reprojection_error_px = reprojection_error;
    QSet<int> observed_frames;
    QVector<Vec3> viewing_directions;
    for (qsizetype index = 8; index < token.size(); index += 2) {
      quint64 image_id = 0;
      bool point_index_ok = false;
      const int point_index = token.at(index + 1).toInt(&point_index_ok);
      if (!parse_unsigned(token.at(index), &image_id) || !point_index_ok || point_index < 0 ||
          !image_index_by_id.contains(image_id)) {
        *error = QStringLiteral("COLMAP 3D point references an invalid image track entry");
        return false;
      }
      const int image_index = image_index_by_id.value(image_id);
      const ParsedImage& image = images->at(image_index);
      if (point_index >= image.observations.size() ||
          image.observations.at(point_index).point_id != scene_point.id) {
        *error = QStringLiteral("COLMAP 3D point track disagrees with image keypoint observations");
        return false;
      }
      // COLMAP may retain repeated keypoints for the same 3D point in one
      // source image. The serialized motion track is source-frame based, so
      // keep one measured observation per frame while still validating every
      // referenced keypoint index.
      if (observed_frames.contains(image.frame_index)) {
        continue;
      }
      observed_frames.insert(image.frame_index);
      const auto& image_observation = image.observations.at(point_index);
      scene_point.observations.append({image.frame_index, image_observation.x, image_observation.y});
      const Vec3 center = camera_center(image.frame);
      Vec3 direction = vector_subtract(point, center);
      const double direction_length = vector_length(direction);
      if (!finite(direction_length) || direction_length < 1.0e-12) {
        *error = QStringLiteral("COLMAP camera center coincides with a reconstructed point");
        return false;
      }
      for (double& component : direction) {
        component /= direction_length;
      }
      viewing_directions.append(direction);
    }
    if (scene_point.observations.size() < 2) {
      continue;
    }
    double max_angle = 0.0;
    for (qsizetype left = 0; left < viewing_directions.size(); ++left) {
      for (qsizetype right = left + 1; right < viewing_directions.size(); ++right) {
        const double cosine = std::clamp(vector_dot(viewing_directions.at(left), viewing_directions.at(right)), -1.0, 1.0);
        max_angle = std::max(max_angle, std::acos(cosine) * 180.0 / kPi);
      }
    }
    triangulation_angles.append(max_angle);
    point_errors.append(reprojection_error);
    scene->points.append(scene_point);
  }
  if (!file.atEnd()) {
    *error = QStringLiteral("COLMAP points3D.txt read failed");
    return false;
  }
  if (scene->points.isEmpty()) {
    *error = QStringLiteral("COLMAP reconstruction contains no tracked 3D points");
    return false;
  }
  scene->quality.registered_frames = static_cast<int>(scene->frames.size());
  scene->quality.total_frames = static_cast<int>(scene->frames.size());
  scene->quality.reprojection_error_px =
      std::accumulate(point_errors.cbegin(), point_errors.cend(), 0.0) /
      static_cast<double>(point_errors.size());
  scene->quality.median_reprojection_error_px = median(point_errors);
  scene->quality.maximum_reprojection_error_px =
      *std::max_element(point_errors.cbegin(), point_errors.cend());
  scene->quality.median_triangulation_angle_degrees = median(triangulation_angles);

  // Keep the durable/QML-facing scene compact. Prefer stable, long tracks and
  // low reprojection error, seed the selection across a 16x12 grid in frame 0,
  // then guarantee at least 20 retained observations in every solved frame.
  QVector<int> ranked(scene->points.size());
  std::iota(ranked.begin(), ranked.end(), 0);
  std::stable_sort(ranked.begin(), ranked.end(), [&scene](int left, int right) {
    const auto& a = scene->points.at(left);
    const auto& b = scene->points.at(right);
    if (a.observations.size() != b.observations.size()) {
      return a.observations.size() > b.observations.size();
    }
    if (a.reprojection_error_px != b.reprojection_error_px) {
      return a.reprojection_error_px < b.reprojection_error_px;
    }
    return a.id < b.id;
  });
  QVector<bool> retained(scene->points.size(), false);
  QVector<int> selected;
  selected.reserve(kMaximumRetainedScenePoints);
  QSet<int> occupied_grid_cells;
  for (const int candidate : ranked) {
    const auto& point = scene->points.at(candidate);
    for (const auto& observation : point.observations) {
      if (observation.frame_index != 0) {
        continue;
      }
      const int x_cell = std::clamp(static_cast<int>(observation.x * 16.0 / scene->width), 0, 15);
      const int y_cell = std::clamp(static_cast<int>(observation.y * 12.0 / scene->height), 0, 11);
      const int grid_cell = y_cell * 16 + x_cell;
      if (!occupied_grid_cells.contains(grid_cell)) {
        occupied_grid_cells.insert(grid_cell);
        retained[candidate] = true;
        selected.append(candidate);
      }
      break;
    }
    if (selected.size() >= kMaximumRetainedScenePoints) {
      break;
    }
  }
  QVector<int> retained_per_frame(scene->frames.size(), 0);
  const auto add_candidate = [&scene, &retained, &selected, &retained_per_frame](int candidate) {
    if (retained.at(candidate) || selected.size() >= kMaximumRetainedScenePoints) {
      return;
    }
    retained[candidate] = true;
    selected.append(candidate);
    for (const auto& observation : scene->points.at(candidate).observations) {
      ++retained_per_frame[observation.frame_index];
    }
  };
  for (const int candidate : selected) {
    for (const auto& observation : scene->points.at(candidate).observations) {
      ++retained_per_frame[observation.frame_index];
    }
  }
  for (int frame_index = 0; frame_index < scene->frames.size(); ++frame_index) {
    for (const int candidate : ranked) {
      if (retained_per_frame.at(frame_index) >= kMinimumFrameObservations) {
        break;
      }
      const auto& observations = scene->points.at(candidate).observations;
      const bool observed = std::any_of(
          observations.cbegin(), observations.cend(), [frame_index](const auto& observation) {
            return observation.frame_index == frame_index;
          });
      if (observed) {
        add_candidate(candidate);
      }
    }
  }
  for (const int candidate : ranked) {
    add_candidate(candidate);
    if (selected.size() >= kMaximumRetainedScenePoints) {
      break;
    }
  }
  QVector<TimelineScenePoint> retained_points;
  retained_points.reserve(selected.size());
  for (const int candidate : selected) {
    retained_points.append(std::move(scene->points[candidate]));
  }
  scene->points = std::move(retained_points);
  std::fill(retained_per_frame.begin(), retained_per_frame.end(), 0);
  for (const auto& point : scene->points) {
    for (const auto& observation : point.observations) {
      ++retained_per_frame[observation.frame_index];
    }
  }
  scene->quality.point_count = static_cast<int>(scene->points.size());
  scene->quality.minimum_frame_observations =
      *std::min_element(retained_per_frame.cbegin(), retained_per_frame.cend());
  for (int frame_index = 0; frame_index < scene->frames.size(); ++frame_index) {
    scene->frames[frame_index].observed_point_count = retained_per_frame.at(frame_index);
  }
  return true;
}

ParseSceneResult parse_text_model(const QString& model_directory,
                                  const TimelineSceneSolveRequest& request,
                                  int width,
                                  int height) {
  ParseSceneResult result;
  ParsedCamera camera;
  const QDir model(model_directory);
  if (!read_camera_model(model.filePath(QStringLiteral("cameras.txt")), &camera, &result.error)) {
    return result;
  }
  if (camera.width != width || camera.height != height) {
    result.error = QStringLiteral("COLMAP model dimensions differ from staged source frames");
    return result;
  }
  QVector<ParsedImage> images;
  QHash<quint64, int> image_index_by_id;
  if (!parse_images(model.filePath(QStringLiteral("images.txt")), request, camera,
                    &images, &image_index_by_id, &result.error)) {
    return result;
  }
  result.scene.width = width;
  result.scene.height = height;
  result.scene.frames.reserve(images.size());
  for (const auto& image : images) {
    result.scene.frames.append(image.frame);
  }
  if (!parse_points(model.filePath(QStringLiteral("points3D.txt")), &images,
                    image_index_by_id, &result.scene, &result.error)) {
    return result;
  }
  result.scene.quality.registered_frames = static_cast<int>(images.size());
  result.scene.quality.total_frames = static_cast<int>(request.source_times_us.size());
  if (result.scene.points.size() < kMinimumScenePoints ||
      result.scene.quality.minimum_frame_observations < kMinimumFrameObservations ||
      result.scene.quality.reprojection_error_px > kMaximumMeanReprojectionErrorPx ||
      result.scene.quality.median_triangulation_angle_degrees < kMinimumMedianTriangulationAngleDegrees) {
    result.scene.status = TimelineSceneStatus::InsufficientCoverage;
    result.scene.message = QStringLiteral(
        "COLMAP solved the views but scene quality is insufficient (points %1, min frame observations %2, "
        "mean reprojection %3px, median parallax %4°)")
                               .arg(result.scene.points.size())
                               .arg(result.scene.quality.minimum_frame_observations)
                               .arg(result.scene.quality.reprojection_error_px, 0, 'f', 2)
                               .arg(result.scene.quality.median_triangulation_angle_degrees, 0, 'f', 2);
    result.ok = true;
    return result;
  }
  result.scene.status = TimelineSceneStatus::Success;
  result.scene.message.clear();
  result.ok = true;
  return result;
}

QString resolved_colmap(const QString& override_path) {
  QString requested = override_path.trimmed();
  if (requested.isEmpty()) {
    requested = qEnvironmentVariable("PHOTO_EDITOR_COLMAP_BINARY").trimmed();
  }
  if (requested.isEmpty()) {
    requested = QStringLiteral("colmap");
  }
  if (QDir::isAbsolutePath(requested) || requested.contains(QLatin1Char('/'))) {
    const QFileInfo info(requested);
    return info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString{};
  }
  auto executable = QStandardPaths::findExecutable(requested);
#ifdef Q_OS_MACOS
  if (executable.isEmpty()) {
    executable = QStandardPaths::findExecutable(
        requested, {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
  }
#endif
  return executable;
}

void append_log_tail(QByteArray* tail, const QByteArray& chunk) {
  constexpr qsizetype kLogTailLimit = 32 * 1024;
  if (chunk.isEmpty()) {
    return;
  }
  tail->append(chunk);
  if (tail->size() > kLogTailLimit) {
    tail->remove(0, tail->size() - kLogTailLimit);
  }
}

struct ProcessResult final {
  TimelineSceneStatus status = TimelineSceneStatus::Failed;
  QString message;
  bool ok = false;
};

ProcessResult run_colmap_process(const QString& program,
                                const QStringList& arguments,
                                const QString& working_directory,
                                QElapsedTimer& total_timer,
                                int timeout_ms,
                                const std::atomic_bool* cancelled) {
  if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
    return {TimelineSceneStatus::Cancelled, QStringLiteral("Scene reconstruction was cancelled"), false};
  }
  QProcess process;
  process.setWorkingDirectory(working_directory);
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(program, arguments, QIODevice::ReadOnly);
  if (!process.waitForStarted(5000)) {
    return {TimelineSceneStatus::Unavailable,
            QStringLiteral("Could not start COLMAP: %1").arg(process.errorString()), false};
  }
  QByteArray log_tail;
  while (process.state() != QProcess::NotRunning) {
    if (cancelled != nullptr && cancelled->load(std::memory_order_relaxed)) {
      process.terminate();
      if (!process.waitForFinished(300)) {
        process.kill();
        process.waitForFinished(3000);
      }
      return {TimelineSceneStatus::Cancelled, QStringLiteral("Scene reconstruction was cancelled"), false};
    }
    if (total_timer.elapsed() >= timeout_ms) {
      process.kill();
      process.waitForFinished(3000);
      append_log_tail(&log_tail, process.readAll());
      return {TimelineSceneStatus::TimedOut,
              QStringLiteral("COLMAP exceeded the scene reconstruction time limit"), false};
    }
    process.waitForReadyRead(100);
    append_log_tail(&log_tail, process.readAll());
  }
  append_log_tail(&log_tail, process.readAll());
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {TimelineSceneStatus::Failed,
            QStringLiteral("COLMAP failed (exit %1): %2")
                .arg(process.exitCode())
                .arg(QString::fromLocal8Bit(log_tail).trimmed()), false};
  }
  return {TimelineSceneStatus::Success, {}, true};
}

bool validate_staged_frames(const TimelineSceneSolveRequest& request,
                            int* width,
                            int* height,
                            QString* error) {
  const int frame_count = static_cast<int>(request.source_times_us.size());
  if (frame_count < 3 || frame_count > kMaximumFrames || request.frames_directory.trimmed().isEmpty()) {
    *error = QStringLiteral("Scene solve requires 3..900 staged frames");
    return false;
  }
  qint64 previous_time = -1;
  for (const qint64 time : request.source_times_us) {
    if (time < 0 || time <= previous_time) {
      *error = QStringLiteral("Source frame times must be nonnegative and strictly increasing");
      return false;
    }
    previous_time = time;
  }
  const QFileInfo directory_info(request.frames_directory);
  if (!directory_info.isDir() || !directory_info.isReadable()) {
    *error = QStringLiteral("Staged scene frame directory is unavailable");
    return false;
  }
  const QStringList staged_jpegs =
      QDir(request.frames_directory)
          .entryList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                      QStringLiteral("*.JPG"), QStringLiteral("*.JPEG")},
                     QDir::Files, QDir::Name);
  if (staged_jpegs.size() != frame_count) {
    *error = QStringLiteral("Scene frame directory must contain exactly the requested JPEG sequence");
    return false;
  }
  int expected_width = -1;
  int expected_height = -1;
  for (int index = 0; index < frame_count; ++index) {
    const QString name = QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'));
    const QString path = QDir(request.frames_directory).filePath(name);
    if (staged_jpegs.at(index) != name) {
      *error = QStringLiteral("Scene frames must be numeric JPEGs from 000000.jpg with no gaps");
      return false;
    }
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || !info.isReadable()) {
      *error = QStringLiteral("A staged scene frame is missing, linked, or unreadable: %1").arg(name);
      return false;
    }
    QImageReader reader(path);
    if (reader.format().toLower() != QByteArrayLiteral("jpeg")) {
      *error = QStringLiteral("Staged scene frames must be JPEG encoded");
      return false;
    }
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || dimensions.width() < 64 || dimensions.height() < 64 ||
        dimensions.width() > kMaximumImageEdge || dimensions.height() > kMaximumImageEdge) {
      *error = QStringLiteral("Staged scene frames must be at least 64px and no larger than 960px per edge");
      return false;
    }
    if (index == 0) {
      expected_width = dimensions.width();
      expected_height = dimensions.height();
    } else if (dimensions.width() != expected_width || dimensions.height() != expected_height) {
      *error = QStringLiteral("All staged scene frames must have identical dimensions");
      return false;
    }
  }
  *width = expected_width;
  *height = expected_height;
  return true;
}

bool largest_sparse_model(const QString& sparse_directory, QString* model_path, quint64* registered) {
  quint64 largest = 0;
  QString selected;
  const QDir sparse(sparse_directory);
  const QStringList children = sparse.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& child : children) {
    const QString candidate = sparse.filePath(child);
    const QFileInfo directory_info(candidate);
    const QFileInfo cameras_info(QDir(candidate).filePath(QStringLiteral("cameras.bin")));
    const QFileInfo images_info(QDir(candidate).filePath(QStringLiteral("images.bin")));
    const QFileInfo points_info(QDir(candidate).filePath(QStringLiteral("points3D.bin")));
    if (directory_info.isSymLink() || cameras_info.isSymLink() || images_info.isSymLink() ||
        points_info.isSymLink() || !cameras_info.isFile() || !images_info.isFile() || !points_info.isFile()) {
      continue;
    }
    QFile images_file(images_info.filePath());
    if (!images_file.open(QIODevice::ReadOnly) || images_file.size() < 8) {
      continue;
    }
    QDataStream stream(&images_file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint64 count = 0;
    stream >> count;
    if (stream.status() == QDataStream::Ok && count > largest) {
      largest = count;
      selected = directory_info.absoluteFilePath();
    }
  }
  if (selected.isEmpty()) {
    return false;
  }
  *model_path = selected;
  *registered = largest;
  return true;
}

bool number_from_variant(const QVariant& value, double* result) {
  bool ok = false;
  const double parsed = value.toDouble(&ok);
  if (!ok || !finite(parsed)) {
    return false;
  }
  *result = parsed;
  return true;
}

bool vector_from_variant(const QVariant& value, Vec3* vector) {
  const QVariantList list = value.toList();
  if (list.size() != 3) {
    return false;
  }
  for (int index = 0; index < 3; ++index) {
    if (!number_from_variant(list.at(index), &(*vector)[static_cast<size_t>(index)])) {
      return false;
    }
  }
  return true;
}

QVariantList variant_vector(const Vec3& vector) {
  return {vector[0], vector[1], vector[2]};
}

QVariantList variant_matrix(const std::array<double, 12>& matrix) {
  QVariantList values;
  values.reserve(12);
  for (double value : matrix) {
    values.append(value);
  }
  return values;
}

bool scene_frame_index_for_time(const QVariantMap& scene, double source_seconds, int* frame_index) {
  if (!finite(source_seconds) || source_seconds < 0.0) {
    return false;
  }
  const QVariantList frames = scene.value(QStringLiteral("frames")).toList();
  double closest_delta = std::numeric_limits<double>::infinity();
  int closest_index = -1;
  for (int index = 0; index < frames.size(); ++index) {
    const QVariantMap frame = frames.at(index).toMap();
    double time = 0.0;
    if (!number_from_variant(frame.value(QStringLiteral("sourceSeconds")), &time)) {
      return false;
    }
    const double delta = std::abs(time - source_seconds);
    if (delta < closest_delta) {
      closest_delta = delta;
      closest_index = index;
    }
  }
  if (closest_index < 0 || closest_delta > 0.00000051) {
    return false;
  }
  *frame_index = closest_index;
  return true;
}

bool scene_projection_header_is_valid(const QVariantMap& scene, QString* error_message) {
  const QVariantList frames = scene.value(QStringLiteral("frames")).toList();
  if (scene.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      scene.value(QStringLiteral("width")).toInt() < 64 ||
      scene.value(QStringLiteral("height")).toInt() < 64 ||
      scene.value(QStringLiteral("width")).toInt() > kMaximumImageEdge ||
      scene.value(QStringLiteral("height")).toInt() > kMaximumImageEdge ||
      frames.size() < 3 || frames.size() > kMaximumFrames ||
      scene.value(QStringLiteral("registeredFraction")).toDouble() != 1.0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Scene projection metadata is incomplete or unsupported");
    }
    return false;
  }
  qint64 previous_time = -1;
  for (int frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const QVariantMap frame = frames.at(frame_index).toMap();
    bool time_ok = false;
    const qint64 source_time = frame.value(QStringLiteral("sourceTimeUs")).toLongLong(&time_ok);
    double source_seconds = 0.0;
    if (!time_ok || source_time < 0 || source_time <= previous_time ||
        !number_from_variant(frame.value(QStringLiteral("sourceSeconds")), &source_seconds) ||
        std::abs(source_seconds - static_cast<double>(source_time) / 1000000.0) > 0.00000051 ||
        frame.value(QStringLiteral("imageName")).toString() !=
            QStringLiteral("%1.jpg").arg(frame_index, 6, 10, QLatin1Char('0'))) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Scene frame times or source image order are invalid");
      }
      return false;
    }
    previous_time = source_time;
  }
  return true;
}

bool project_world_point(const QVariantMap& frame, const Vec3& point, Vec3* pixel_and_depth) {
  const QVariantList matrix_list = frame.value(QStringLiteral("worldToCamera")).toList();
  if (matrix_list.size() != 12) {
    return false;
  }
  std::array<double, 12> matrix{};
  for (int index = 0; index < 12; ++index) {
    if (!number_from_variant(matrix_list.at(index), &matrix[static_cast<size_t>(index)])) {
      return false;
    }
  }
  const double x = matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3];
  const double y = matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7];
  const double z = matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11];
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  if (z <= 1.0e-9 || !number_from_variant(frame.value(QStringLiteral("fx")), &fx) ||
      !number_from_variant(frame.value(QStringLiteral("fy")), &fy) ||
      !number_from_variant(frame.value(QStringLiteral("cx")), &cx) ||
      !number_from_variant(frame.value(QStringLiteral("cy")), &cy)) {
    return false;
  }
  const double px = fx * x / z + cx;
  const double py = fy * y / z + cy;
  if (!finite(px) || !finite(py)) {
    return false;
  }
  *pixel_and_depth = {px, py, z};
  return true;
}

}  // namespace

TimelineSceneResult solveTimelineScene(const TimelineSceneSolveRequest& request,
                                       const TimelineSceneSolveOptions& options) {
  if (options.timeout_ms <= 0 || options.timeout_ms > 60 * 60 * 1000) {
    return result_with_status(TimelineSceneStatus::InvalidInput,
                              QStringLiteral("Scene reconstruction timeout must be 1ms..60min"));
  }
  int width = 0;
  int height = 0;
  QString input_error;
  if (!validate_staged_frames(request, &width, &height, &input_error)) {
    return result_with_status(TimelineSceneStatus::InvalidInput, input_error);
  }
  if (options.cancelled != nullptr && options.cancelled->load(std::memory_order_relaxed)) {
    return result_with_status(TimelineSceneStatus::Cancelled,
                              QStringLiteral("Scene reconstruction was cancelled"));
  }
  const QString colmap = resolved_colmap(options.colmap_executable);
  if (colmap.isEmpty()) {
    return result_with_status(TimelineSceneStatus::Unavailable,
                              QStringLiteral("COLMAP was not found; install it or set PHOTO_EDITOR_COLMAP_BINARY"));
  }

  QString temp_parent = options.workspace_directory.trimmed();
  if (temp_parent.isEmpty()) {
    temp_parent = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  }
  if (temp_parent.isEmpty() || !QDir().mkpath(temp_parent)) {
    return result_with_status(TimelineSceneStatus::Unavailable,
                              QStringLiteral("Cannot create a temporary COLMAP workspace"));
  }
  QTemporaryDir workspace(QDir(temp_parent).filePath(QStringLiteral("timeline-scene-XXXXXX")));
  if (!workspace.isValid()) {
    return result_with_status(TimelineSceneStatus::Unavailable,
                              QStringLiteral("Cannot allocate an isolated COLMAP workspace"));
  }
  const QString database = QDir(workspace.path()).filePath(QStringLiteral("features.db"));
  const QString sparse = QDir(workspace.path()).filePath(QStringLiteral("sparse"));
  const QString text_model = QDir(workspace.path()).filePath(QStringLiteral("text"));
  if (!QDir().mkpath(sparse) || !QDir().mkpath(text_model)) {
    return result_with_status(TimelineSceneStatus::Unavailable,
                              QStringLiteral("Cannot create COLMAP workspace folders"));
  }

  const QStringList stage_names{
      QStringLiteral("extracting-features"),
      QStringLiteral("matching-sequential-views"),
      QStringLiteral("solving-camera-path"),
      QStringLiteral("exporting-validated-scene"),
  };
  const int frame_count = static_cast<int>(request.source_times_us.size());
  const auto report_progress = [&options, &stage_names](int completed) {
    if (options.progress) {
      const QString stage = completed == stage_names.size()
                                ? QStringLiteral("validating-scene")
                                : stage_names.at(completed);
      options.progress(stage, completed, static_cast<int>(stage_names.size()));
    }
  };
  QElapsedTimer total_timer;
  total_timer.start();
  QStringList feature_args{
      QStringLiteral("feature_extractor"),
      QStringLiteral("--image_path"), request.frames_directory,
      QStringLiteral("--database_path"), database,
      QStringLiteral("--ImageReader.single_camera"), QStringLiteral("1"),
      QStringLiteral("--ImageReader.camera_model"), QStringLiteral("SIMPLE_PINHOLE"),
      QStringLiteral("--FeatureExtraction.use_gpu"), QStringLiteral("0"),
      QStringLiteral("--FeatureExtraction.num_threads"), QStringLiteral("4"),
      QStringLiteral("--SiftExtraction.max_image_size"), QString::number(kMaximumImageEdge),
  };
  QStringList matching_args{
      QStringLiteral("sequential_matcher"),
      QStringLiteral("--database_path"), database,
      QStringLiteral("--FeatureMatching.use_gpu"), QStringLiteral("0"),
      QStringLiteral("--FeatureMatching.num_threads"), QStringLiteral("4"),
      QStringLiteral("--FeatureMatching.guided_matching"), QStringLiteral("1"),
      QStringLiteral("--SequentialMatching.overlap"), QString::number(std::min(20, frame_count - 1)),
      QStringLiteral("--SequentialMatching.quadratic_overlap"), QStringLiteral("1"),
      QStringLiteral("--SequentialMatching.loop_detection"), QStringLiteral("0"),
  };
  QStringList mapper_args{
      QStringLiteral("mapper"),
      QStringLiteral("--image_path"), request.frames_directory,
      QStringLiteral("--database_path"), database,
      QStringLiteral("--output_path"), sparse,
      QStringLiteral("--Mapper.num_threads"), QStringLiteral("4"),
      QStringLiteral("--Mapper.multiple_models"), QStringLiteral("1"),
      QStringLiteral("--Mapper.min_model_size"), QStringLiteral("3"),
      QStringLiteral("--Mapper.ba_use_gpu"), QStringLiteral("0"),
      QStringLiteral("--Mapper.ba_refine_principal_point"), QStringLiteral("0"),
  };
  report_progress(0);
  ProcessResult process_result = run_colmap_process(
      colmap, feature_args, workspace.path(), total_timer, options.timeout_ms, options.cancelled);
  if (!process_result.ok) {
    return result_with_status(process_result.status, process_result.message);
  }
  report_progress(1);
  process_result = run_colmap_process(
      colmap, matching_args, workspace.path(), total_timer, options.timeout_ms, options.cancelled);
  if (!process_result.ok) {
    return result_with_status(process_result.status, process_result.message);
  }
  report_progress(2);
  process_result = run_colmap_process(
      colmap, mapper_args, workspace.path(), total_timer, options.timeout_ms, options.cancelled);
  if (!process_result.ok) {
    return result_with_status(process_result.status, process_result.message);
  }

  QString selected_model;
  quint64 registered_count = 0;
  if (!largest_sparse_model(sparse, &selected_model, &registered_count) || registered_count < 3) {
    return result_with_status(TimelineSceneStatus::InsufficientCoverage,
                              QStringLiteral("COLMAP could not reconstruct at least three scene views"));
  }
  if (registered_count != static_cast<quint64>(frame_count)) {
    return result_with_status(
        TimelineSceneStatus::InsufficientCoverage,
        QStringLiteral("COLMAP registered %1 of %2 requested frames; camera-path gaps cannot be interpolated")
            .arg(registered_count)
            .arg(frame_count));
  }
  report_progress(3);
  const QStringList converter_args{
      QStringLiteral("model_converter"),
      QStringLiteral("--input_path"), selected_model,
      QStringLiteral("--output_path"), text_model,
      QStringLiteral("--output_type"), QStringLiteral("TXT"),
  };
  process_result = run_colmap_process(
      colmap, converter_args, workspace.path(), total_timer, options.timeout_ms, options.cancelled);
  if (!process_result.ok) {
    return result_with_status(process_result.status, process_result.message);
  }
  report_progress(4);

  ParseSceneResult parsed = parse_text_model(text_model, request, width, height);
  if (!parsed.ok) {
    const bool coverage_failure = parsed.error.contains(QStringLiteral("unregistered"), Qt::CaseInsensitive) ||
                                  parsed.error.contains(QStringLiteral("register every"), Qt::CaseInsensitive);
    return result_with_status(coverage_failure ? TimelineSceneStatus::InsufficientCoverage
                                              : TimelineSceneStatus::InvalidOutput,
                              parsed.error);
  }
  return parsed.scene;
}

QVariantMap timelineSceneResultToMap(const TimelineSceneResult& result) {
  if (result.status != TimelineSceneStatus::Success || result.frames.isEmpty() || result.points.isEmpty()) {
    return {};
  }
  QVariantList frames;
  frames.reserve(result.frames.size());
  for (const auto& frame : result.frames) {
    frames.append(QVariantMap{
        {QStringLiteral("sourceTimeUs"), frame.source_time_us},
        {QStringLiteral("sourceSeconds"), static_cast<double>(frame.source_time_us) / 1000000.0},
        {QStringLiteral("imageName"), frame.image_name},
        {QStringLiteral("worldToCamera"), variant_matrix(frame.world_to_camera)},
        {QStringLiteral("fx"), frame.fx}, {QStringLiteral("fy"), frame.fy},
        {QStringLiteral("cx"), frame.cx}, {QStringLiteral("cy"), frame.cy},
        {QStringLiteral("k1"), frame.k1},
        {QStringLiteral("observedPointCount"), frame.observed_point_count},
    });
  }
  QVariantList points;
  points.reserve(result.points.size());
  for (const auto& point : result.points) {
    QVariantList observations;
    observations.reserve(point.observations.size());
    for (const auto& observation : point.observations) {
      observations.append(QVariantMap{
          {QStringLiteral("frameIndex"), observation.frame_index},
          {QStringLiteral("x"), observation.x},
          {QStringLiteral("y"), observation.y},
      });
    }
    points.append(QVariantMap{
        {QStringLiteral("id"), point.id},
        {QStringLiteral("x"), point.world_position[0]},
        {QStringLiteral("y"), point.world_position[1]},
        {QStringLiteral("z"), point.world_position[2]},
        {QStringLiteral("rgb"), QVariantList{point.color_rgb[0], point.color_rgb[1], point.color_rgb[2]}},
        {QStringLiteral("error"), point.reprojection_error_px},
        {QStringLiteral("observations"), observations},
    });
  }
  return {
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("width"), result.width},
      {QStringLiteral("height"), result.height},
      {QStringLiteral("frames"), frames},
      {QStringLiteral("points"), points},
      {QStringLiteral("registeredFraction"), result.quality.total_frames > 0
                                                   ? static_cast<double>(result.quality.registered_frames) /
                                                         static_cast<double>(result.quality.total_frames)
                                                   : 0.0},
      {QStringLiteral("reprojectionError"), result.quality.reprojection_error_px},
      {QStringLiteral("medianReprojectionError"), result.quality.median_reprojection_error_px},
      {QStringLiteral("maximumReprojectionError"), result.quality.maximum_reprojection_error_px},
      {QStringLiteral("medianTriangulationAngleDegrees"), result.quality.median_triangulation_angle_degrees},
      {QStringLiteral("minimumFrameObservations"), result.quality.minimum_frame_observations},
  };
}

bool timelineSceneIsValid(const QVariantMap& scene, QString* error_message) {
  const auto reject = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return false;
  };
  if (scene.value(QStringLiteral("schemaVersion")).toInt() != 1) {
    return reject(QStringLiteral("Unsupported scene tracking schema version"));
  }
  const int width = scene.value(QStringLiteral("width")).toInt();
  const int height = scene.value(QStringLiteral("height")).toInt();
  if (width < 64 || height < 64 || width > kMaximumImageEdge || height > kMaximumImageEdge) {
    return reject(QStringLiteral("Scene source dimensions are outside the supported range"));
  }
  const QVariantList frames = scene.value(QStringLiteral("frames")).toList();
  const QVariantList points = scene.value(QStringLiteral("points")).toList();
  if (frames.size() < 3 || frames.size() > kMaximumFrames || points.size() < kMinimumScenePoints ||
      points.size() > kMaximumSparsePoints ||
      scene.value(QStringLiteral("registeredFraction")).toDouble() != 1.0) {
    return reject(QStringLiteral("Scene is incomplete or lacks the required camera/point coverage"));
  }
  qint64 previous_time = -1;
  QVector<int> observations_per_frame(frames.size(), 0);
  for (int frame_index = 0; frame_index < frames.size(); ++frame_index) {
    const QVariantMap frame = frames.at(frame_index).toMap();
    bool time_ok = false;
    const qint64 source_time_us = frame.value(QStringLiteral("sourceTimeUs")).toLongLong(&time_ok);
    double source_seconds = 0.0;
    if (!time_ok || source_time_us < 0 || source_time_us <= previous_time ||
        !number_from_variant(frame.value(QStringLiteral("sourceSeconds")), &source_seconds) ||
        std::abs(source_seconds - static_cast<double>(source_time_us) / 1000000.0) > 0.00000051 ||
        frame.value(QStringLiteral("imageName")).toString() !=
            QStringLiteral("%1.jpg").arg(frame_index, 6, 10, QLatin1Char('0'))) {
      return reject(QStringLiteral("Scene frame times or source image order are invalid"));
    }
    previous_time = source_time_us;
    const QVariantList matrix = frame.value(QStringLiteral("worldToCamera")).toList();
    if (matrix.size() != 12) {
      return reject(QStringLiteral("Camera pose must be a row-major 3x4 world-to-camera matrix"));
    }
    std::array<double, 12> m{};
    for (int index = 0; index < 12; ++index) {
      if (!number_from_variant(matrix.at(index), &m[static_cast<size_t>(index)])) {
        return reject(QStringLiteral("Camera pose contains a non-finite value"));
      }
    }
    const Vec3 row0{m[0], m[1], m[2]};
    const Vec3 row1{m[4], m[5], m[6]};
    const Vec3 row2{m[8], m[9], m[10]};
    const double determinant = row0[0] * (row1[1] * row2[2] - row1[2] * row2[1]) -
                               row0[1] * (row1[0] * row2[2] - row1[2] * row2[0]) +
                               row0[2] * (row1[0] * row2[1] - row1[1] * row2[0]);
    if (std::abs(vector_length(row0) - 1.0) > 1.0e-3 ||
        std::abs(vector_length(row1) - 1.0) > 1.0e-3 ||
        std::abs(vector_length(row2) - 1.0) > 1.0e-3 ||
        std::abs(vector_dot(row0, row1)) > 1.0e-3 ||
        std::abs(vector_dot(row0, row2)) > 1.0e-3 ||
        std::abs(vector_dot(row1, row2)) > 1.0e-3 || std::abs(determinant - 1.0) > 1.0e-3) {
      return reject(QStringLiteral("Camera pose rotation is not orthonormal"));
    }
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double k1 = 0.0;
    if (!number_from_variant(frame.value(QStringLiteral("fx")), &fx) || fx <= 0.0 ||
        !number_from_variant(frame.value(QStringLiteral("fy")), &fy) || fy <= 0.0 ||
        !number_from_variant(frame.value(QStringLiteral("cx")), &cx) || cx < -width || cx > 2 * width ||
        !number_from_variant(frame.value(QStringLiteral("cy")), &cy) || cy < -height || cy > 2 * height ||
        !number_from_variant(frame.value(QStringLiteral("k1")), &k1) || std::abs(k1) > 1.0e-12) {
      return reject(QStringLiteral("Scene requires finite zero-distortion PINHOLE intrinsics"));
    }
  }
  QSet<QString> point_ids;
  QVector<double> errors;
  errors.reserve(points.size());
  for (const QVariant& point_variant : points) {
    const QVariantMap point = point_variant.toMap();
    const QString id = point.value(QStringLiteral("id")).toString();
    Vec3 world{};
    double point_error = 0.0;
    if (id.isEmpty() || point_ids.contains(id) ||
        !vector_from_variant(QVariantList{point.value(QStringLiteral("x")), point.value(QStringLiteral("y")),
                                          point.value(QStringLiteral("z"))}, &world) ||
        !number_from_variant(point.value(QStringLiteral("error")), &point_error) || point_error < 0.0) {
      return reject(QStringLiteral("Scene point ID, coordinates, or reprojection error are invalid"));
    }
    point_ids.insert(id);
    errors.append(point_error);
    const QVariantList observations = point.value(QStringLiteral("observations")).toList();
    if (observations.size() < 2) {
      return reject(QStringLiteral("Every scene point must be observed by at least two solved frames"));
    }
    QSet<int> seen_frames;
    for (const QVariant& observation_variant : observations) {
      const QVariantMap observation = observation_variant.toMap();
      bool frame_ok = false;
      const int frame_index = observation.value(QStringLiteral("frameIndex")).toInt(&frame_ok);
      double x = 0.0;
      double y = 0.0;
      if (!frame_ok || frame_index < 0 || frame_index >= frames.size() || seen_frames.contains(frame_index) ||
          !number_from_variant(observation.value(QStringLiteral("x")), &x) || x < 0.0 || x > width ||
          !number_from_variant(observation.value(QStringLiteral("y")), &y) || y < 0.0 || y > height) {
        return reject(QStringLiteral("Scene point observations contain invalid frame or pixel coordinates"));
      }
      seen_frames.insert(frame_index);
      ++observations_per_frame[frame_index];
    }
  }
  const int min_observations = *std::min_element(observations_per_frame.cbegin(), observations_per_frame.cend());
  for (int frame_index = 0; frame_index < frames.size(); ++frame_index) {
    bool count_ok = false;
    const int declared_count = frames.at(frame_index).toMap()
                                  .value(QStringLiteral("observedPointCount")).toInt(&count_ok);
    if (!count_ok || declared_count != observations_per_frame.at(frame_index)) {
      return reject(QStringLiteral("Scene per-frame observation totals do not match their point tracks"));
    }
  }
  double reprojection_error = 0.0;
  if (!number_from_variant(scene.value(QStringLiteral("reprojectionError")), &reprojection_error) ||
      reprojection_error < 0.0 || reprojection_error > kMaximumMeanReprojectionErrorPx ||
      min_observations < kMinimumFrameObservations) {
    return reject(QStringLiteral("Scene reprojection or per-frame point coverage is below quality requirements"));
  }
  double triangulation_angle = 0.0;
  if (!number_from_variant(scene.value(QStringLiteral("medianTriangulationAngleDegrees")), &triangulation_angle) ||
      triangulation_angle < kMinimumMedianTriangulationAngleDegrees) {
    return reject(QStringLiteral("Scene does not contain sufficient multi-view parallax"));
  }
  double median_reprojection_error = 0.0;
  double maximum_reprojection_error = 0.0;
  if (!number_from_variant(scene.value(QStringLiteral("medianReprojectionError")), &median_reprojection_error) ||
      median_reprojection_error < 0.0 || median_reprojection_error > reprojection_error + 1.0e-6 ||
      !number_from_variant(scene.value(QStringLiteral("maximumReprojectionError")), &maximum_reprojection_error) ||
      maximum_reprojection_error < median_reprojection_error || maximum_reprojection_error > 10.0) {
    return reject(QStringLiteral("Scene reprojection quality metrics are inconsistent"));
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

QVariantList timelineSceneVisiblePoints(const QVariantMap& scene,
                                        double source_seconds,
                                        QString* error_message) {
  const auto reject = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return QVariantList{};
  };
  QString validation_error;
  if (!timelineSceneIsValid(scene, &validation_error)) {
    return reject(validation_error);
  }
  int frame_index = -1;
  if (!scene_frame_index_for_time(scene, source_seconds, &frame_index)) {
    return reject(QStringLiteral("No solved source-time camera exists at this exact frame"));
  }
  const int width = scene.value(QStringLiteral("width")).toInt();
  const int height = scene.value(QStringLiteral("height")).toInt();
  QVariantList visible;
  for (const QVariant& point_variant : scene.value(QStringLiteral("points")).toList()) {
    const QVariantMap point = point_variant.toMap();
    for (const QVariant& observation_variant : point.value(QStringLiteral("observations")).toList()) {
      const QVariantMap observation = observation_variant.toMap();
      if (observation.value(QStringLiteral("frameIndex")).toInt() != frame_index) {
        continue;
      }
      const Vec3 world{point.value(QStringLiteral("x")).toDouble(),
                       point.value(QStringLiteral("y")).toDouble(),
                       point.value(QStringLiteral("z")).toDouble()};
      visible.append(QVariantMap{
          {QStringLiteral("id"), point.value(QStringLiteral("id"))},
          {QStringLiteral("world"), variant_vector(world)},
          {QStringLiteral("source"), QVariantList{observation.value(QStringLiteral("x")).toDouble() / width,
                                                  observation.value(QStringLiteral("y")).toDouble() / height}},
          {QStringLiteral("error"), point.value(QStringLiteral("error"))},
      });
      break;
    }
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return visible;
}

QVariantMap timelineSceneAnchorFromPoints(const QVariantMap& scene,
                                          const QStringList& selected_point_ids,
                                          QString* error_message) {
  const auto reject = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return QVariantMap{};
  };
  QString validation_error;
  if (!timelineSceneIsValid(scene, &validation_error)) {
    return reject(validation_error);
  }
  if (selected_point_ids.size() != 3 || selected_point_ids.at(0) == selected_point_ids.at(1) ||
      selected_point_ids.at(0) == selected_point_ids.at(2) ||
      selected_point_ids.at(1) == selected_point_ids.at(2)) {
    return reject(QStringLiteral("Choose three different reconstructed points to define the attachment plane"));
  }
  QHash<QString, Vec3> point_positions;
  const QVariantList points = scene.value(QStringLiteral("points")).toList();
  for (const QVariant& point_variant : points) {
    const QVariantMap point = point_variant.toMap();
    point_positions.insert(point.value(QStringLiteral("id")).toString(),
                           {point.value(QStringLiteral("x")).toDouble(),
                            point.value(QStringLiteral("y")).toDouble(),
                            point.value(QStringLiteral("z")).toDouble()});
  }
  if (!point_positions.contains(selected_point_ids.at(0)) ||
      !point_positions.contains(selected_point_ids.at(1)) ||
      !point_positions.contains(selected_point_ids.at(2))) {
    return reject(QStringLiteral("Selected point IDs are not present in the solved scene"));
  }
  const Vec3 center = point_positions.value(selected_point_ids.at(0));
  const Vec3 p1 = point_positions.value(selected_point_ids.at(1));
  const Vec3 p2 = point_positions.value(selected_point_ids.at(2));
  Vec3 axis_x = vector_subtract(p1, center);
  const double width = vector_length(axis_x);
  if (!finite(width) || width < 1.0e-9) {
    return reject(QStringLiteral("Selected points are too close together to define an attachment width"));
  }
  for (double& component : axis_x) {
    component /= width;
  }
  Vec3 axis_y = vector_subtract(p2, center);
  const double projection = vector_dot(axis_y, axis_x);
  for (int index = 0; index < 3; ++index) {
    axis_y[static_cast<size_t>(index)] -= projection * axis_x[static_cast<size_t>(index)];
  }
  const double height = vector_length(axis_y);
  if (!finite(height) || height < std::max(width * 1.0e-4, 1.0e-9)) {
    return reject(QStringLiteral("Selected points are collinear and cannot define a stable attachment plane"));
  }
  for (double& component : axis_y) {
    component /= height;
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return {
      {QStringLiteral("center"), variant_vector(center)},
      {QStringLiteral("axisX"), variant_vector(axis_x)},
      {QStringLiteral("axisY"), variant_vector(axis_y)},
      {QStringLiteral("width"), width},
      {QStringLiteral("height"), width},
      {QStringLiteral("selectedPointIds"), selected_point_ids},
  };
}

QVariantList timelineSceneProjectAnchor(const QVariantMap& scene,
                                        const QVariantMap& anchor,
                                        double source_seconds,
                                        QString* error_message) {
  const auto reject = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return QVariantList{};
  };
  QString validation_error;
  if (!scene_projection_header_is_valid(scene, &validation_error)) {
    return reject(validation_error);
  }
  int frame_index = -1;
  if (!scene_frame_index_for_time(scene, source_seconds, &frame_index)) {
    return reject(QStringLiteral("No solved source-time camera exists at this exact frame"));
  }
  Vec3 center{};
  Vec3 axis_x{};
  Vec3 axis_y{};
  double width = 0.0;
  double height = 0.0;
  if (!vector_from_variant(anchor.value(QStringLiteral("center")), &center) ||
      !vector_from_variant(anchor.value(QStringLiteral("axisX")), &axis_x) ||
      !vector_from_variant(anchor.value(QStringLiteral("axisY")), &axis_y) ||
      !number_from_variant(anchor.value(QStringLiteral("width")), &width) || width <= 0.0 ||
      !number_from_variant(anchor.value(QStringLiteral("height")), &height) || height <= 0.0) {
    return reject(QStringLiteral("Attachment anchor basis or dimensions are invalid"));
  }
  if (std::abs(vector_length(axis_x) - 1.0) > 1.0e-3 ||
      std::abs(vector_length(axis_y) - 1.0) > 1.0e-3 ||
      std::abs(vector_dot(axis_x, axis_y)) > 1.0e-3) {
    return reject(QStringLiteral("Attachment anchor axes must be orthonormal"));
  }
  const QVariantList selected_ids = anchor.value(QStringLiteral("selectedPointIds")).toList();
  if (selected_ids.size() != 3 || selected_ids.at(0).toString().isEmpty() ||
      selected_ids.at(0).toString() == selected_ids.at(1).toString() ||
      selected_ids.at(0).toString() == selected_ids.at(2).toString() ||
      selected_ids.at(1).toString() == selected_ids.at(2).toString()) {
    return reject(QStringLiteral("Attachment anchor must retain its three distinct scene point IDs"));
  }
  const auto offset_point = [&center, &axis_x, &axis_y](double x, double y) {
    return Vec3{center[0] + axis_x[0] * x + axis_y[0] * y,
                center[1] + axis_x[1] * x + axis_y[1] * y,
                center[2] + axis_x[2] * x + axis_y[2] * y};
  };
  const double half_width = width * 0.5;
  const double half_height = height * 0.5;
  const std::array<Vec3, 4> corners{
      offset_point(-half_width, -half_height),
      offset_point(half_width, -half_height),
      offset_point(-half_width, half_height),
      offset_point(half_width, half_height),
  };
  const QVariantMap frame = scene.value(QStringLiteral("frames")).toList().at(frame_index).toMap();
  const int source_width = scene.value(QStringLiteral("width")).toInt();
  const int source_height = scene.value(QStringLiteral("height")).toInt();
  QVariantList projected;
  projected.reserve(4);
  for (const Vec3& corner : corners) {
    Vec3 pixel_depth{};
    if (!project_world_point(frame, corner, &pixel_depth)) {
      return reject(QStringLiteral("Attachment corner lies behind the camera or cannot be projected"));
    }
    const double normalized_x = pixel_depth[0] / source_width;
    const double normalized_y = pixel_depth[1] / source_height;
    projected.append(QVariant::fromValue(QVariantList{normalized_x, normalized_y}));
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  return projected;
}

}  // namespace photo_editor::desktop
