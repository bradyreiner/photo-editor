#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QVariantMap>

#include "timeline_scene_runtime.h"

using namespace photo_editor::desktop;

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QString fixture_directory = qEnvironmentVariable("PHOTO_EDITOR_SCENE_TRACKING_FIXTURE");
  if (fixture_directory.isEmpty()) {
    fixture_directory = QStringLiteral("/private/tmp/looksy-scene-tracking-proof/frames");
  }
  const QDir fixture(fixture_directory);
  if (!fixture.exists()) {
    std::cerr << "SKIP: set PHOTO_EDITOR_SCENE_TRACKING_FIXTURE to a directory of 16 parallax PNGs\n";
    return 77;
  }
  QTemporaryDir staging;
  if (!staging.isValid()) {
    return fail(QStringLiteral("Could not allocate staged COLMAP fixture frames"));
  }
  TimelineSceneSolveRequest request;
  request.frames_directory = staging.path();
  for (int index = 0; index < 16; ++index) {
    const QString source_path = fixture.filePath(QStringLiteral("frame_%1.png").arg(index, 6, 10, QLatin1Char('0')));
    const QImage image(source_path);
    if (image.isNull() || image.width() != 640 || image.height() != 480 ||
        !image.save(QDir(staging.path()).filePath(QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'))),
                    "JPEG", 98)) {
      return fail(QStringLiteral("Could not stage fixture frame %1 as a 640x480 JPEG").arg(index));
    }
    request.source_times_us.append(static_cast<qint64>(index) * 125000);
  }

  TimelineSceneSolveOptions options;
  options.timeout_ms = 10 * 60 * 1000;
  options.colmap_executable = qEnvironmentVariable("PHOTO_EDITOR_COLMAP_BINARY");
  int progress_events = 0;
  int final_completed_stages = 0;
  options.progress = [&progress_events, &final_completed_stages](const QString&, int completed, int total) {
    ++progress_events;
    if (total == 4) {
      final_completed_stages = completed;
    }
  };
  const TimelineSceneResult result = solveTimelineScene(request, options);
  if (result.status == TimelineSceneStatus::Unavailable) {
    std::cerr << "SKIP: " << result.message.toStdString() << '\n';
    return 77;
  }
  if (result.status != TimelineSceneStatus::Success) {
    return fail(QStringLiteral("Native COLMAP fixture solve failed: %1").arg(result.message));
  }
  if (result.frames.size() != 16 || result.quality.registered_frames != 16 ||
      result.quality.total_frames != 16 || result.quality.point_count < 50 ||
      result.quality.reprojection_error_px > 3.0 ||
      result.quality.median_triangulation_angle_degrees < 1.0 || progress_events != 5 ||
      final_completed_stages != 4) {
    return fail(QStringLiteral("Native COLMAP fixture did not meet all-frame parallax quality gates"));
  }
  const QVariantMap scene = timelineSceneResultToMap(result);
  const QString result_path = qEnvironmentVariable("PHOTO_EDITOR_SCENE_TRACKING_RESULT");
  if (!result_path.isEmpty()) {
    QSaveFile output(result_path);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument::fromVariant(scene).toJson(QJsonDocument::Compact)) < 0 || !output.commit()) {
      return fail(QStringLiteral("Could not write requested native scene result JSON"));
    }
  }
  QString error;
  if (!timelineSceneIsValid(scene, &error)) {
    return fail(QStringLiteral("Native COLMAP scene failed schema validation: %1").arg(error));
  }
  const QVariantList visible = timelineSceneVisiblePoints(scene, 0.0, &error);
  if (visible.size() < 50 || !error.isEmpty()) {
    return fail(QStringLiteral("Native COLMAP source-time observations failed: %1").arg(error));
  }
  QVariantMap anchor;
  int attempts = 0;
  for (qsizetype left = 0; left < visible.size() && anchor.isEmpty() && attempts < 24; ++left) {
    for (qsizetype middle = left + 1; middle < visible.size() && anchor.isEmpty() && attempts < 24; ++middle) {
      for (qsizetype right = middle + 1; right < visible.size(); ++right) {
        ++attempts;
        anchor = timelineSceneAnchorFromPoints(
            scene,
            {visible.at(left).toMap().value(QStringLiteral("id")).toString(),
             visible.at(middle).toMap().value(QStringLiteral("id")).toString(),
             visible.at(right).toMap().value(QStringLiteral("id")).toString()},
            &error);
        if (!anchor.isEmpty()) {
          break;
        }
      }
    }
  }
  if (anchor.isEmpty()) {
    return fail(QStringLiteral("Native COLMAP scene has no non-collinear source-visible anchor: %1").arg(error));
  }
  const QVariantList first_quad = timelineSceneProjectAnchor(scene, anchor, 0.0, &error);
  const QVariantList last_quad = timelineSceneProjectAnchor(scene, anchor, 1.875, &error);
  if (first_quad.size() != 4 || last_quad.size() != 4 || !error.isEmpty()) {
    return fail(QStringLiteral("Native COLMAP camera path did not project both source endpoints: %1").arg(error));
  }
  const double first_left = first_quad.first().toList().first().toDouble();
  const double last_left = last_quad.first().toList().first().toDouble();
  if (qAbs(first_left - last_left) < 0.01) {
    return fail(QStringLiteral("Native COLMAP camera path did not retain measurable scene parallax"));
  }
  return EXIT_SUCCESS;
}
