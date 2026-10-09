#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QVector>

#include <array>
#include <atomic>
#include <functional>

namespace photo_editor::desktop {

enum class TimelineSceneStatus {
  Success,
  Unavailable,
  InvalidInput,
  Cancelled,
  TimedOut,
  Failed,
  InsufficientCoverage,
  InvalidOutput,
};

struct TimelineSceneObservation final {
  int frame_index = -1;
  double x = 0.0;  // COLMAP source-image pixel coordinates, top-left origin.
  double y = 0.0;
};

struct TimelineScenePoint final {
  QString id;  // COLMAP IDs are unsigned 64-bit values; keep their exact text form.
  std::array<double, 3> world_position{};
  std::array<int, 3> color_rgb{};
  double reprojection_error_px = 0.0;
  QVector<TimelineSceneObservation> observations;
};

struct TimelineSceneFrame final {
  qint64 source_time_us = -1;
  QString image_name;
  // Row-major world-to-camera 3x4 matrix. Coordinates follow COLMAP:
  // camera +X right, +Y down, +Z forward.
  std::array<double, 12> world_to_camera{};
  double fx = 0.0;
  double fy = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  double k1 = 0.0;  // This runtime currently solves PINHOLE, so k1 must remain zero.
  int observed_point_count = 0;
};

struct TimelineSceneQuality final {
  int registered_frames = 0;
  int total_frames = 0;
  int point_count = 0;
  int minimum_frame_observations = 0;
  double reprojection_error_px = 0.0;
  double median_reprojection_error_px = 0.0;
  double maximum_reprojection_error_px = 0.0;
  double median_triangulation_angle_degrees = 0.0;
};

struct TimelineSceneResult final {
  TimelineSceneStatus status = TimelineSceneStatus::Failed;
  int width = 0;
  int height = 0;
  QVector<TimelineSceneFrame> frames;
  QVector<TimelineScenePoint> points;
  TimelineSceneQuality quality;
  QString message;
};

struct TimelineSceneSolveRequest final {
  QString frames_directory;
  // Strictly increasing source-video timestamps, one for each 000000.jpg, etc.
  QVector<qint64> source_times_us;
};

struct TimelineSceneSolveOptions final {
  int timeout_ms = 20 * 60 * 1000;
  const std::atomic_bool* cancelled = nullptr;
  // Reports a pipeline stage and completed stage count (0..4).
  std::function<void(const QString& stage, int completed_stages, int total_stages)> progress;
  QString colmap_executable;
  QString workspace_directory;
};

// Runs CPU SIFT + sequential matching + incremental mapper and converts only
// a fully validated PINHOLE reconstruction to an editable, source-time scene.
// Call from a worker thread; input must contain exactly the numeric JPEGs
// 000000.jpg through N-1, all <=960px on the longest side.
[[nodiscard]] TimelineSceneResult solveTimelineScene(
    const TimelineSceneSolveRequest& request,
    const TimelineSceneSolveOptions& options = {});

// Versioned QVariantMap schema used by controller persistence and QML adapters.
[[nodiscard]] QVariantMap timelineSceneResultToMap(const TimelineSceneResult& result);
[[nodiscard]] bool timelineSceneIsValid(const QVariantMap& scene, QString* error_message = nullptr);

// Returns reconstructed points observed in the frame nearest sourceSeconds
// (within 0.5us), as maps {id, world:[x,y,z], source:[normalizedX,normalizedY], error}.
[[nodiscard]] QVariantList timelineSceneVisiblePoints(
    const QVariantMap& scene,
    double source_seconds,
    QString* error_message = nullptr);

// Three selected 3D point IDs define an oriented plane. P0 is its center,
// P1 defines axisX and initial width, and the orthogonalized P2 direction
// defines axisY; initial height equals width and may be adjusted by the caller.
[[nodiscard]] QVariantMap timelineSceneAnchorFromPoints(
    const QVariantMap& scene,
    const QStringList& selected_point_ids,
    QString* error_message = nullptr);

// Projects an anchor through the exact source-time camera into normalized
// top-left source coordinates ordered TL, TR, BL, BR. No interpolation or
// extrapolation is performed; hidden/behind-camera quads return an error.
[[nodiscard]] QVariantList timelineSceneProjectAnchor(
    const QVariantMap& scene,
    const QVariantMap& anchor,
    double source_seconds,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop
