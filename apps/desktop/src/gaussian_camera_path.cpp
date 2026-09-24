#include "gaussian_camera_path.h"
#include "gaussian_saved_views.h"
#include <QJsonObject>
#include <QQuaternion>
#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {
QQuaternion orientation(const GaussianCamera& camera) {
  QMatrix3x3 matrix;
  for (int row = 0; row < 3; ++row) {
    matrix(row, 0) = camera.right[row]; matrix(row, 1) = camera.down[row]; matrix(row, 2) = camera.forward[row];
  }
  return QQuaternion::fromRotationMatrix(matrix).normalized();
}
}
int GaussianCameraPath::frameCount() const { return static_cast<int>(std::round(duration() * fps)); }
GaussianCamera GaussianCameraPath::sample(double seconds) const {
  if (cameras.empty()) return {};
  if (!std::isfinite(seconds) || seconds <= 0) return cameras.front();
  if (seconds >= duration()) return cameras.back();
  const auto segment = std::min(static_cast<size_t>(seconds / segment_seconds), cameras.size() - 2);
  const auto& a = cameras[segment]; const auto& b = cameras[segment + 1];
  const auto fraction = static_cast<float>((seconds - static_cast<double>(segment) * segment_seconds) / segment_seconds);
  // Ease to a stop at each authored keyframe; shortest-arc orientation, no Euler wrap.
  const auto t = fraction * fraction * (3.0F - 2.0F * fraction);
  if (t == 0) return a;
  if (t == 1) return b;
  auto result = a;
  result.name = "Camera path";
  result.position = a.position * (1 - t) + b.position * t;
  const auto rotation = QQuaternion::slerp(orientation(a), orientation(b), t).normalized();
  result.right = rotation.rotatedVector({1, 0, 0});
  result.down = rotation.rotatedVector({0, 1, 0});
  result.forward = rotation.rotatedVector({0, 0, 1});
  result.fx = a.fx * (1 - t) + b.fx * t; result.fy = a.fy * (1 - t) + b.fy * t;
  result.cx = a.cx * (1 - t) + b.cx * t; result.cy = a.cy * (1 - t) + b.cy * t;
  result.photo.focal_scale = a.photo.focal_scale * (1 - t) + b.photo.focal_scale * t;
  result.photo.exposure_ev = a.photo.exposure_ev * (1 - t) + b.photo.exposure_ev * t;
  result.photo.lens.focus_distance = a.photo.lens.focus_distance * (1 - t) + b.photo.lens.focus_distance * t;
  result.photo.lens.aperture_radius = a.photo.lens.aperture_radius * (1 - t) + b.photo.lens.aperture_radius * t;
  return result;
}
std::optional<GaussianCameraPath> gaussian_camera_path(const QVariantMap& settings, const QVariantList& views, QString* error) {
  const auto fail = [&](const QString& reason) -> std::optional<GaussianCameraPath> { if (error) *error = reason; return {}; };
  const auto json = QJsonObject::fromVariantMap(settings);
  const auto ids = settings.value("viewIds").toList();
  const auto seconds = json.value("segmentSeconds").toDouble(-1);
  const auto fps = json.value("fps").toDouble(-1);
  if (!json.value("viewIds").isArray() || ids.size() < 2 || ids.size() > 128 ||
      !std::isfinite(seconds) || seconds < 0.5 || seconds > 60 || seconds * static_cast<double>(ids.size() - 1) > 600 ||
      (fps != 24 && fps != 25 && fps != 30 && fps != 60))
    return fail("Choose 2–128 shots, 0.5–60 seconds per transition, at most 10 minutes, and 24/25/30/60 fps.");
  GaussianCameraPath path; path.segment_seconds = seconds; path.fps = static_cast<int>(fps);
  for (const auto& id : ids) {
    if (id.metaType().id() != QMetaType::QString || id.toString().isEmpty()) return fail("Invalid path shot ID.");
    std::optional<GaussianCamera> camera;
    for (const auto& view : views) if (view.toMap().value("id") == id) {
      camera = gaussian_saved_camera(view.toMap().value("camera").toMap()); break;
    }
    if (!camera) return fail("A camera-path shot is missing or invalid.");
    if (!path.view_ids.isEmpty() && path.view_ids.back() == id.toString()) return fail("Adjacent path shots must differ.");
    if (!path.cameras.empty()) {
      if (camera->physical != path.cameras.front().physical)
        return fail("Physical camera paths currently require the same scale, sensor gate, focal length, f-stop and focus in every shot. Camera movement and exposure may vary.");
      const auto& lens = camera->photo.lens;
      const auto& first_lens = path.cameras.front().photo.lens;
      if (lens.blades != first_lens.blades || lens.rotation_degrees != first_lens.rotation_degrees ||
          lens.roundness != first_lens.roundness || lens.samples != first_lens.samples)
        return fail("Path shots must share aperture shape, rotation and sampling quality. Focus and aperture size may vary.");
      if (camera->photo.look != path.cameras.front().photo.look)
        return fail("Path shots must use the same photographic look. Exposure and lens framing may vary.");
      if (camera->photo.film != path.cameras.front().photo.film)
        return fail("Path shots must use the same grain and halation settings, including seed.");
      const auto size = path.cameras.front().image_size;
      const double aspect = static_cast<double>(size.width()) / size.height();
      const double other = static_cast<double>(camera->image_size.width()) / camera->image_size.height();
      if (std::abs(aspect - other) > 0.001) return fail("Path shots must share the same aspect ratio.");
      *camera = camera->resized(size);
    }
    path.cameras.push_back(*camera); path.view_ids.append(id.toString());
  }
  return path;
}
}  // namespace photo_editor::desktop
