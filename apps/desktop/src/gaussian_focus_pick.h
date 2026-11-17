#pragma once
#include "gaussian_scene_data.h"
#include "gaussian_lens_projection.h"
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
    QPointF pixel, std::optional<double> seconds, const std::shared_ptr<std::atomic_bool>& canceled, double pixel_variance = 0.3) {
  const auto fail = [](const QString& message) { return QVariantMap{{"ok", false}, {"error", message}}; };
  const auto interrupted = [&] { return canceled && canceled->load(); };
  QElapsedTimer elapsed; elapsed.start();
  if (interrupted()) return {{"ok", false}, {"canceled", true}};
  const double fx = camera.fx * camera.photo.focal_scale, fy = camera.fy * camera.photo.focal_scale;
  if (!valid_gaussian_alpha_model(gaussian_alpha_model(camera)) || !std::isfinite(pixel_variance) || pixel_variance < 0 || scene.points.empty() || scene.points.size() > gaussian_static_point_limit || !camera.photo.valid() ||
      !std::isfinite(fx) || !std::isfinite(fy) || fx <= 0 || fy <= 0 ||
      !std::isfinite(pixel.x()) || !std::isfinite(pixel.y()) || pixel.x() < 0 || pixel.y() < 0 ||
      pixel.x() >= camera.image_size.width() || pixel.y() >= camera.image_size.height()) return fail("Invalid focus-pick camera or pixel.");
  const auto display_pixel = pixel;
  const double focal_factor = 1+(camera.photo.lens.field_curvature+camera.photo.lens.spherical_distortion)*
      gaussian_lens_field_weight(gaussian_lens_field(camera,pixel));
  if (focal_factor <= 0) return fail("This field position cannot focus in front of the lens. Reduce field curvature or distortion.");
  pixel = gaussian_lens_chief_pixel(camera,pixel);
  const auto focus_ray = (camera.forward + camera.right*static_cast<float>((pixel.x()-camera.cx)/fx) +
      camera.down*static_cast<float>((pixel.y()-camera.cy)/fy)).normalized();
  const double ray_forward = QVector3D::dotProduct(focus_ray,camera.forward);
  if (focus_ray.isNull() || !std::isfinite(ray_forward) || ray_forward <= 1e-6) return fail("This focus ray does not point into the scene.");
  const bool temporal = !scene.temporal.empty();
  if (seconds && !std::isfinite(*seconds)) return fail("Focus picking requires a finite capture time.");
  if (temporal && (scene.temporal.size() != scene.points.size() || !seconds || !std::isfinite(*seconds) ||
      !std::isfinite(scene.time_origin_seconds) || !std::isfinite(scene.local_time_range[0]) || !std::isfinite(scene.local_time_range[1]) ||
      scene.local_time_range[1] <= scene.local_time_range[0] || *seconds-scene.time_origin_seconds < scene.local_time_range[0] ||
      *seconds-scene.time_origin_seconds > scene.local_time_range[1])) return fail("Focus picking requires a valid capture time.");
  struct Hit { double depth, alpha; size_t index; };
  std::vector<Hit> hits;
  if (camera.composition) {
    const auto& ray = focus_ray;
    double nearest = 1e30;
    for (const auto& object : camera.composition->objects) {
      if (!object.visible) continue;
      const auto inverse = QQuaternion::fromEulerAngles(object.rotation).conjugated();
      auto origin = inverse.rotatedVector(camera.position-object.position), direction = inverse.rotatedVector(ray);
      for (int axis = 0; axis < 3; ++axis) { origin[axis] /= object.size[axis]; direction[axis] /= object.size[axis]; }
      double distance = 1e30;
      if (object.kind == 2) {
        if (!object.mesh) continue;
        distance = gaussian_mesh_hit(*object.mesh,origin,direction);
      } else if (object.kind == 0) {
        const double a = QVector3D::dotProduct(direction,direction), b = QVector3D::dotProduct(origin,direction);
        const double c = QVector3D::dotProduct(origin,origin)-1, discriminant = b*b-a*c;
        if (discriminant < 0 || a <= 0) continue;
        distance = (-b-std::sqrt(discriminant))/a;
        if (distance <= 1e-5) distance = (-b+std::sqrt(discriminant))/a;
      } else {
        double enter = -1e30, leave = 1e30;
        for (int axis = 0; axis < 3; ++axis) {
          if (std::abs(direction[axis]) < 1e-12) { if (std::abs(origin[axis]) > 1) leave = -1e30; continue; }
          const double a = (-1.0-origin[axis])/direction[axis], b = (1.0-origin[axis])/direction[axis];
          enter = std::max(enter,std::min(a,b)); leave = std::min(leave,std::max(a,b));
        }
        if (leave < std::max(enter,1e-5)) continue;
        distance = enter > 1e-5 ? enter : leave;
      }
      const double depth = distance*QVector3D::dotProduct(ray,camera.forward);
      if (distance > 1e-5 && depth >= 0.01) nearest = std::min(nearest,depth);
    }
    for (const auto& light : camera.composition->lights) {
      if (!light.primaryVisible()) continue;
      const auto offset = camera.position-light.vector;
      double distance = 1e30;
      if (light.kind == 3) {
        double a = 0, b = 0; std::array<double,3> origin{}, direction{};
        for (int axis = 0; axis < 3; ++axis) {
          const auto index = static_cast<size_t>(axis);
          origin[index] = static_cast<double>(camera.position[axis])-light.vector[axis]; direction[index] = ray[axis];
          a += direction[index]*direction[index]; b += origin[index]*direction[index];
        }
        if (a <= 0) continue;
        double perpendicular_squared = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
          const auto cross = origin[(axis+1)%3]*direction[(axis+2)%3]-origin[(axis+2)%3]*direction[(axis+1)%3];
          perpendicular_squared += cross*cross/a;
        }
        const double discriminant = static_cast<double>(light.radius)*light.radius-perpendicular_squared;
        if (discriminant < 0) continue;
        const double root = std::sqrt(a*discriminant);
        distance = (-b-root)/a;
        if (distance <= 1e-5) distance = (-b+root)/a;
      } else {
        const auto rotation = QQuaternion::fromEulerAngles(light.rotation);
        const auto normal = rotation.rotatedVector({0,0,1});
        const double denominator = QVector3D::dotProduct(ray,normal);
        if (std::abs(denominator) < 1e-8) continue;
        distance = -QVector3D::dotProduct(offset,normal)/denominator;
        if (distance <= 1e-5) continue;
        const auto hit = offset+ray*static_cast<float>(distance);
        if (std::abs(QVector3D::dotProduct(hit,rotation.rotatedVector({1,0,0}))) > light.size.x()*0.5 ||
            std::abs(QVector3D::dotProduct(hit,rotation.rotatedVector({0,1,0}))) > light.size.y()*0.5) continue;
      }
      const double depth = distance*QVector3D::dotProduct(ray,camera.forward);
      if (distance > 1e-5 && depth >= 0.01) nearest = std::min(nearest,depth);
    }
    if (nearest < 1e29) hits.push_back({nearest,1,scene.points.size()});
  }
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
    double a = pixel_variance, b = 0, c = pixel_variance;
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
    const double alpha = std::min(static_cast<double>(gaussian_alpha_limit(gaussian_alpha_model(camera))), opacity*std::exp(-power));
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
  const auto world_point = camera.position + focus_ray*static_cast<float>(selected/ray_forward);
  return {{"ok", true}, {"model", "projected-opacity-median-v1"}, {"focusDistance", selected*focal_factor}, {"surfaceDepth",selected},
      {"opacity", 1-transmission}, {"overlappingSplats", static_cast<qulonglong>(hits.size())},
      {"worldPoint", QVariantList{world_point.x(),world_point.y(),world_point.z()}},
      {"pixelX", display_pixel.x()}, {"pixelY", display_pixel.y()}, {"elapsedMs", elapsed.elapsed()}, {"distanceUnit", "scene-units"}};
}
}  // namespace photo_editor::desktop
