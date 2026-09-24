#pragma once
#include "gaussian_scene_data.h"
#include <QImage>
#include <optional>
#include <mutex>
#include <utility>

namespace photo_editor::desktop {
struct GaussianFrame {
  quint64 native_texture = 0;
  std::shared_ptr<void> native_lease;
  QSize size;
  QImage image;  // Only populated for explicit export/readback, never preview.
  QVariantMap metrics;
  QString error;
  bool canceled = false;
};
// Latest-only queue: at most one unpublished immutable GPU snapshot. The
// worker never accesses a viewport/QObject, including during its destruction.
class GaussianProgressiveFrames {
 public:
  void publish(GaussianFrame frame) {
    std::lock_guard lock(mutex_);
    latest_ = std::move(frame);
  }
  std::optional<GaussianFrame> takeLatest() {
    std::lock_guard lock(mutex_);
    return std::exchange(latest_, std::nullopt);
  }
 private:
  std::mutex mutex_;
  std::optional<GaussianFrame> latest_;
};
struct GaussianRenderOptions {
  std::shared_ptr<std::atomic_bool> canceled;
  std::shared_ptr<std::atomic_int> samples_completed;
  bool interactive = false;
  // Explicit photo/readback only. Zero chooses the renderer's bounded default;
  // a smaller tile can reduce export GPU working memory without changing framing.
  int export_tile_edge = 0;
  std::shared_ptr<std::atomic_int> tiles_completed{};
  std::shared_ptr<GaussianProgressiveFrames> progressive_frames{};
};
class GaussianSceneRenderer {
 public:
  virtual ~GaussianSceneRenderer() = default;
  // Serialized worker use; no GUI-thread GPU waits or preview readback.
  virtual GaussianFrame render(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds = {}) = 0;
  virtual GaussianFrame renderWithOptions(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds, const GaussianRenderOptions&) {
    return render(scene, camera, readback, scene_seconds);
  }
};
std::shared_ptr<GaussianSceneRenderer> make_gaussian_scene_renderer();
}  // namespace photo_editor::desktop
