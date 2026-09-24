#pragma once
#include "gaussian_scene_data.h"
#include <optional>

namespace photo_editor::desktop {
struct GaussianCameraPath {
  std::vector<GaussianCamera> cameras;
  QStringList view_ids;
  double segment_seconds = 3;
  int fps = 24;
  double duration() const { return cameras.size() < 2 ? 0 : segment_seconds * static_cast<double>(cameras.size() - 1); }
  int frameCount() const;
  GaussianCamera sample(double seconds) const;
};
std::optional<GaussianCameraPath> gaussian_camera_path(const QVariantMap& settings,
    const QVariantList& views, QString* error = nullptr);
}  // namespace photo_editor::desktop
