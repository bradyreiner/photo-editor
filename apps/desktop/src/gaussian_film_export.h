#pragma once
#include "gaussian_camera_path.h"
#include "gaussian_scene_renderer.h"

namespace photo_editor::desktop {
struct GaussianFilmProgress {
  std::atomic_bool canceled{false};
  std::atomic_int completed{0};
};
struct GaussianFilmSceneTime {
  double start_seconds = 0;
  double end_seconds = 0;
};
enum class GaussianFilmFormat { Mp4, PngSequence };
QVariantMap export_gaussian_film(const std::shared_ptr<const GaussianSceneData>& scene,
    const GaussianCameraPath& path, const QString& destination, int maximum_edge,
    const QString& ffmpeg, const std::shared_ptr<GaussianFilmProgress>& progress,
    std::optional<GaussianFilmSceneTime> scene_time = {}, GaussianFilmFormat format = GaussianFilmFormat::Mp4);
}  // namespace photo_editor::desktop
