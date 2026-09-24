#pragma once
#include "gaussian_scene_data.h"
#include <QElapsedTimer>
#include <QPointF>
#include <QSizeF>
#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
inline std::optional<QPointF> gaussian_focus_pixel(QSize image, QSizeF viewport, QPointF item) {
  if (image.isEmpty() || viewport.isEmpty() || !std::isfinite(viewport.width()) || !std::isfinite(viewport.height()) ||
      !std::isfinite(item.x()) || !std::isfinite(item.y())) return {};
  QSizeF shown = image; shown.scale(viewport, Qt::KeepAspectRatio);
  const QPointF origin((viewport.width()-shown.width())/2, (viewport.height()-shown.height())/2);
  const auto x = (item.x()-origin.x())*image.width()/shown.width();
  const auto y = (item.y()-origin.y())*image.height()/shown.height();
  if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) return {};
  return QPointF(std::floor(x)+0.5, std::floor(y)+0.5);
}

// Worker-only pinhole opacity-median estimate. Uses projected Gaussian centers,
// not a mesh intersection or measured scene scale. Never reads back a preview.
inline QVariantMap pick_gaussian_focus(const GaussianSceneData& scene, const GaussianCamera& camera,
    QPointF pixel, std::optional<double> seconds, const std::shared_ptr<std::atomic_bool>& canceled) {
  const auto fail = [](const QString& message) { return QVariantMap{{"ok", false}, {"error", message}}; };
  const auto interrupted = [&] { return canceled && canceled->load(); };
  QElapsedTimer elapsed; elapsed.start();
  if (interrupted()) return {{"ok", false}, {"canceled", true}};
  const double fx = camera.fx * camera.photo.focal_scale, fy = camera.fy * camera.photo.focal_scale;
  if (scene.points.empty() || scene.points.size() > 2000000 || !camera.photo.valid() ||
      !std::isfinite(fx) || !std::isfinite(fy) || fx <= 0 || fy <= 0 ||
      !std::isfinite(pixel.x()) || !std::isfinite(pixel.y()) || pixel.x() < 0 || pixel.y() < 0 ||
      pixel.x() >= camera.image_size.width() || pixel.y() >= camera.image_size.height()) return fail("Invalid focus-pick camera or pixel.");
  const bool temporal = !scene.temporal.empty();
  if (seconds && !std::isfinite(*seconds)) return fail("Focus picking requires a finite capture time.");
  if (temporal && (scene.temporal.size() != scene.points.size() || !seconds || !std::isfinite(*seconds) ||
      !std::isfinite(scene.time_origin_seconds) || !std::isfinite(scene.local_time_range[0]) || !std::isfinite(scene.local_time_range[1]) ||
      scene.local_time_range[1] <= scene.local_time_range[0] || *seconds-scene.time_origin_seconds < scene.local_time_range[0] ||
      *seconds-scene.time_origin_seconds > scene.local_time_range[1])) return fail("Focus picking requires a valid capture time.");
  struct Hit { double depth, alpha; size_t index; };
  std::vector<Hit> hits;
  const auto component = [](QVector3D v, size_t i) { return static_cast<double>(v[static_cast<int>(i)]); };
  for (size_t index = 0; index < scene.points.size(); ++index) {
    if ((index & 1023U) == 0 && interrupted()) return {{"ok", false}, {"canceled", true}};
    const auto& p = scene.points[index];
    QVector3D center(p.center_opacity[0], p.center_opacity[1], p.center_opacity[2]);
    double opacity = p.center_opacity[3];
    if (temporal) {
      const auto& motion = scene.temporal[index];
      const double dt = *seconds-scene.time_origin_seconds-motion.velocity_time[3];
      const double t = dt*motion.normalization[0];
      opacity *= std::exp(-0.5*t*t);
      center += QVector3D(motion.velocity_time[0], motion.velocity_time[1], motion.velocity_time[2])*static_cast<float>(dt);
    }
    const auto displacement = center-camera.position;
    const double z = QVector3D::dotProduct(camera.forward, displacement);
    if (!std::isfinite(z) || z < 0.01 || z > 1e8 || !std::isfinite(opacity) || opacity < 1.0/255) continue;
    const double u = QVector3D::dotProduct(camera.right, displacement)/z;
    const double v = QVector3D::dotProduct(camera.down, displacement)/z;
    const double limited_u = std::clamp(u, (-0.15*camera.image_size.width()-camera.cx)/fx,
        (1.15*camera.image_size.width()-camera.cx)/fx);
    const double limited_v = std::clamp(v, (-0.15*camera.image_size.height()-camera.cy)/fy,
        (1.15*camera.image_size.height()-camera.cy)/fy);
    std::array<double, 3> dx{}, dy{};
    for (size_t i = 0; i < 3; ++i) {
      dx[i] = fx/z*(component(camera.right,i)-limited_u*component(camera.forward,i));
      dy[i] = fy/z*(component(camera.down,i)-limited_v*component(camera.forward,i));
    }
    double a = 0.3, b = 0, c = 0.3;
    if (temporal) {
      const auto& factor = scene.temporal[index].covariance_factor;
      for (size_t col = 0; col < 3; ++col) {
        double px = 0, py = 0;
        for (size_t row = 0; row < 3; ++row) { px += dx[row]*factor[4*col+row]; py += dy[row]*factor[4*col+row]; }
        a += px*px; b += px*py; c += py*py;
      }
    } else {
      const double covariance[3][3]{{p.covariance_a[0], p.covariance_a[1], p.covariance_a[2]},
          {p.covariance_a[1], p.covariance_a[3], p.covariance_b[0]},
          {p.covariance_a[2], p.covariance_b[0], p.covariance_b[1]}};
      for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 3; ++j) {
        a += dx[i]*covariance[i][j]*dx[j]; b += dx[i]*covariance[i][j]*dy[j]; c += dy[i]*covariance[i][j]*dy[j];
      }
    }
    const double determinant = a*c-b*b, x = pixel.x()-(fx*u+camera.cx), y = pixel.y()-(fy*v+camera.cy);
    if (!std::isfinite(determinant) || determinant <= 0) continue;
    const double power = (c*x*x-2*b*x*y+a*y*y)/(2*determinant);
    if (!std::isfinite(power) || power < 0 || power > std::log(255*opacity)) continue;
    const double alpha = std::min(0.999, opacity*std::exp(-power));
    if (alpha >= 1.0/255) hits.push_back({z, alpha, index});
  }
  std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
    return a.depth == b.depth ? a.index < b.index : a.depth < b.depth;
  });
  if (interrupted()) return {{"ok", false}, {"canceled", true}};
  double transmission = 1, selected = 0;
  for (const auto& hit : hits) {
    transmission *= 1-hit.alpha;
    if (selected == 0 && transmission <= 0.5) selected = hit.depth;
    if (transmission <= 1e-4) break;
  }
  if (selected == 0) return fail("No sufficiently opaque surface here. Pick a solid part of the scene.");
  return {{"ok", true}, {"model", "projected-opacity-median-v1"}, {"focusDistance", selected},
      {"opacity", 1-transmission}, {"overlappingSplats", static_cast<qulonglong>(hits.size())},
      {"pixelX", pixel.x()}, {"pixelY", pixel.y()}, {"elapsedMs", elapsed.elapsed()}, {"distanceUnit", "scene-units"}};
}
}  // namespace photo_editor::desktop
