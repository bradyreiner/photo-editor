#include <cstdlib>
#include <iostream>
#include <atomic>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QVariantMap>

#include "timeline_scene_runtime.h"

using namespace photo_editor::desktop;

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QVariantList vector_list(double x, double y, double z) {
  return {x, y, z};
}

TimelineSceneResult make_scene_result() {
  TimelineSceneResult result;
  result.status = TimelineSceneStatus::Success;
  result.width = 640;
  result.height = 480;
  result.quality.registered_frames = 3;
  result.quality.total_frames = 3;
  result.quality.point_count = 50;
  result.quality.minimum_frame_observations = 50;
  result.quality.reprojection_error_px = 0.45;
  result.quality.median_reprojection_error_px = 0.4;
  result.quality.maximum_reprojection_error_px = 1.0;
  result.quality.median_triangulation_angle_degrees = 5.0;
  const std::array<double, 3> camera_centers{-0.5, 0.0, 0.5};
  for (int frame_index = 0; frame_index < 3; ++frame_index) {
    TimelineSceneFrame frame;
    frame.source_time_us = 1000000 + static_cast<qint64>(frame_index) * 125000;
    frame.image_name = QStringLiteral("%1.jpg").arg(frame_index, 6, 10, QLatin1Char('0'));
    frame.world_to_camera = {
        1.0, 0.0, 0.0, -camera_centers.at(static_cast<size_t>(frame_index)),
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    };
    frame.fx = 400.0;
    frame.fy = 400.0;
    frame.cx = 320.0;
    frame.cy = 240.0;
    frame.observed_point_count = 50;
    result.frames.append(frame);
  }
  for (int point_index = 0; point_index < 50; ++point_index) {
    TimelineScenePoint point;
    point.id = QString::number(point_index + 1);
    if (point_index == 0) {
      point.world_position = {0.0, 0.0, 5.0};
    } else if (point_index == 1) {
      point.world_position = {1.0, 0.0, 5.0};
    } else if (point_index == 2) {
      point.world_position = {0.0, 1.0, 5.0};
    } else if (point_index == 3) {
      point.world_position = {2.0, 0.0, 5.0};
    } else {
      const int grid_index = point_index - 4;
      point.world_position = {
          -1.0 + static_cast<double>(grid_index % 10) * 0.22,
          -0.6 + static_cast<double>(grid_index / 10) * 0.25,
          5.5 + static_cast<double>(grid_index % 4) * 0.2,
      };
    }
    point.color_rgb = {180, 120, 80};
    point.reprojection_error_px = 0.45;
    for (int frame_index = 0; frame_index < 3; ++frame_index) {
      const double x = point.world_position[0] - camera_centers.at(static_cast<size_t>(frame_index));
      const double y = point.world_position[1];
      const double z = point.world_position[2];
      point.observations.append({frame_index, 400.0 * x / z + 320.0, 400.0 * y / z + 240.0});
    }
    result.points.append(point);
  }
  return result;
}

QVariantList quad(const QVariantList& value) {
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const TimelineSceneResult result = make_scene_result();
  const QVariantMap scene = timelineSceneResultToMap(result);
  QString error;
  if (!timelineSceneIsValid(scene, &error)) {
    return fail(QStringLiteral("Serialized synthetic scene did not validate: %1").arg(error));
  }
  const QVariantList visible = timelineSceneVisiblePoints(scene, 1.125, &error);
  if (visible.size() != 50 || !error.isEmpty()) {
    return fail(QStringLiteral("Source-time point lookup failed: %1").arg(error));
  }
  const QVariantMap anchor = timelineSceneAnchorFromPoints(scene, {"1", "2", "3"}, &error);
  if (anchor.isEmpty() || !error.isEmpty() || qAbs(anchor.value(QStringLiteral("width")).toDouble() - 1.0) > 1e-9) {
    return fail(QStringLiteral("Three-point plane construction failed: %1").arg(error));
  }
  QVariantMap sized_anchor = anchor;
  sized_anchor.insert(QStringLiteral("width"), 0.8);
  sized_anchor.insert(QStringLiteral("height"), 0.4);
  const QVariantList first_quad = quad(timelineSceneProjectAnchor(scene, sized_anchor, 1.0, &error));
  if (first_quad.size() != 4 || !error.isEmpty()) {
    return fail(QStringLiteral("First source-time plane projection failed (corners=%1): %2")
                    .arg(first_quad.size()).arg(error));
  }
  const QVariantList last_quad = quad(timelineSceneProjectAnchor(scene, sized_anchor, 1.25, &error));
  if (last_quad.size() != 4 || !error.isEmpty()) {
    return fail(QStringLiteral("Last source-time plane projection failed: %1").arg(error));
  }
  const double first_left = first_quad.at(0).toList().at(0).toDouble();
  const double last_left = last_quad.at(0).toList().at(0).toDouble();
  if (first_left <= last_left + 0.04) {
    return fail(QStringLiteral("Scene plane projection did not follow the camera's true translated pose"));
  }
  if (!timelineSceneProjectAnchor(scene, sized_anchor, 1.06, &error).isEmpty() || error.isEmpty()) {
    return fail(QStringLiteral("Scene projection must not interpolate unsolved source times"));
  }

  const QVariantMap collinear = timelineSceneAnchorFromPoints(scene, {"1", "2", "4"}, &error);
  if (!collinear.isEmpty() || error.isEmpty()) {
    return fail(QStringLiteral("Collinear scene points must not define a plane"));
  }
  QVariantMap behind_camera = sized_anchor;
  behind_camera.insert(QStringLiteral("center"), vector_list(0.0, 0.0, -10.0));
  if (!timelineSceneProjectAnchor(scene, behind_camera, 1.0, &error).isEmpty() || error.isEmpty()) {
    return fail(QStringLiteral("A plane behind the camera must not produce a screen quad"));
  }
  QVariantMap pan_only = scene;
  pan_only.insert(QStringLiteral("medianTriangulationAngleDegrees"), 0.0);
  if (timelineSceneIsValid(pan_only, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Pure rotation without multi-view parallax must be rejected"));
  }
  QVariantMap missing_frame = scene;
  missing_frame.insert(QStringLiteral("registeredFraction"), 2.0 / 3.0);
  if (timelineSceneIsValid(missing_frame, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Scene maps with unregistered frame gaps must be rejected"));
  }
  QVariantMap bad_intrinsics = scene;
  QVariantList frames = bad_intrinsics.value(QStringLiteral("frames")).toList();
  QVariantMap malformed_frame = frames.first().toMap();
  malformed_frame.insert(QStringLiteral("k1"), 0.1);
  frames[0] = malformed_frame;
  bad_intrinsics.insert(QStringLiteral("frames"), frames);
  if (timelineSceneIsValid(bad_intrinsics, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Distorted camera models must not be accepted by the planar quad helper"));
  }
  QVariantMap duplicate_ids = scene;
  QVariantList points = duplicate_ids.value(QStringLiteral("points")).toList();
  QVariantMap duplicate_point = points.at(1).toMap();
  duplicate_point.insert(QStringLiteral("id"), QStringLiteral("1"));
  points[1] = duplicate_point;
  duplicate_ids.insert(QStringLiteral("points"), points);
  if (timelineSceneIsValid(duplicate_ids, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Duplicate reconstructed point IDs must be rejected"));
  }

  QVariantMap offscreen_anchor = sized_anchor;
  offscreen_anchor.insert(QStringLiteral("center"), vector_list(50.0, 0.0, 5.0));
  if (timelineSceneProjectAnchor(scene, offscreen_anchor, 1.0, &error).size() != 4 || !error.isEmpty()) {
    return fail(QStringLiteral("A valid scene plane must remain projectable when fully offscreen"));
  }
  QVariantMap inconsistent_observations = scene;
  frames = inconsistent_observations.value(QStringLiteral("frames")).toList();
  malformed_frame = frames.first().toMap();
  malformed_frame.insert(QStringLiteral("observedPointCount"), 49);
  frames[0] = malformed_frame;
  inconsistent_observations.insert(QStringLiteral("frames"), frames);
  if (timelineSceneIsValid(inconsistent_observations, &error) || error.isEmpty()) {
    return fail(QStringLiteral("Scene frame observation totals must agree with actual point tracks"));
  }

  QTemporaryDir staged_directory;
  if (!staged_directory.isValid()) {
    return fail(QStringLiteral("Could not allocate runtime preflight images"));
  }
  TimelineSceneSolveRequest request;
  request.frames_directory = staged_directory.path();
  for (int index = 0; index < 3; ++index) {
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(qRgb(40 + index * 30, 90, 140));
    const QString file_name = QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'));
    if (!image.save(QDir(staged_directory.path()).filePath(file_name), "JPEG", 95)) {
      return fail(QStringLiteral("Could not create runtime preflight JPEGs"));
    }
    request.source_times_us.append(static_cast<qint64>(index) * 125000);
  }
  std::atomic_bool cancelled{true};
  TimelineSceneSolveOptions preflight_options;
  preflight_options.cancelled = &cancelled;
  if (solveTimelineScene(request, preflight_options).status != TimelineSceneStatus::Cancelled) {
    return fail(QStringLiteral("A pre-cancelled scene solve must return Cancelled before launching COLMAP"));
  }
  cancelled.store(false, std::memory_order_relaxed);
  preflight_options.colmap_executable = QStringLiteral("/definitely/not/a/colmap/binary");
  if (solveTimelineScene(request, preflight_options).status != TimelineSceneStatus::Unavailable) {
    return fail(QStringLiteral("A missing COLMAP binary must return Unavailable instead of a fake solve"));
  }
  request.source_times_us[1] = request.source_times_us[0];
  if (solveTimelineScene(request, preflight_options).status != TimelineSceneStatus::InvalidInput) {
    return fail(QStringLiteral("Non-monotonic source timestamps must reject a scene solve"));
  }
  return EXIT_SUCCESS;
}
