#pragma once
#include "gaussian_scene_data.h"
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>

namespace gaussian_reference {
using Vector = std::array<double, 3>;
inline double dot(Vector a, Vector b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
inline Vector vector(QVector3D value) { return {value.x(), value.y(), value.z()}; }
struct Projected {
  double x, y, a, b, c, alpha, depth;
  Vector color;
};

// Scalar double-precision oracle, independent of Metal's triangle interpolation
// and framebuffer fetch. World-space projection derivatives avoid the GPU's
// intermediate camera covariance matrix. This is a test, never a preview path.
inline QVariantMap audit(const photo_editor::desktop::GaussianSceneData& scene,
    const photo_editor::desktop::GaussianCamera& camera, const QImage& native, const QImage& trainer) {
  std::vector<Projected> points;
  points.reserve(scene.points.size());
  const auto right = vector(camera.right), down = vector(camera.down), forward = vector(camera.forward);
  const auto eye = vector(camera.position);
  for (const auto& p : scene.points) {
    Vector displacement{p.center_opacity[0] - eye[0], p.center_opacity[1] - eye[1], p.center_opacity[2] - eye[2]};
    const double z = dot(forward, displacement);
    if (z < 0.01 || z > 1e10 || p.center_opacity[3] < 1.0 / 255.0) continue;
    const double x = dot(right, displacement) / z, y = dot(down, displacement) / z;
    const double u = std::clamp(x, (-0.15 * camera.image_size.width() - camera.cx) / camera.fx,
        (1.15 * camera.image_size.width() - camera.cx) / camera.fx);
    const double v = std::clamp(y, (-0.15 * camera.image_size.height() - camera.cy) / camera.fy,
        (1.15 * camera.image_size.height() - camera.cy) / camera.fy);
    Vector dx{}, dy{};
    for (size_t i = 0; i < 3; ++i) {
      dx[i] = camera.fx / z * (right[i] - u * forward[i]);
      dy[i] = camera.fy / z * (down[i] - v * forward[i]);
    }
    const double covariance[3][3]{{p.covariance_a[0], p.covariance_a[1], p.covariance_a[2]},
        {p.covariance_a[1], p.covariance_a[3], p.covariance_b[0]},
        {p.covariance_a[2], p.covariance_b[0], p.covariance_b[1]}};
    double a = 0.3, b = 0, c = 0.3;
    for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 3; ++j) {
      a += dx[i] * covariance[i][j] * dx[j];
      b += dx[i] * covariance[i][j] * dy[j];
      c += dy[i] * covariance[i][j] * dy[j];
    }
    const double det = a*c - b*b;
    if (det <= 0) continue;
    const double length = std::sqrt(dot(displacement, displacement));
    const double nx = displacement[0] / length, ny = displacement[1] / length, nz = displacement[2] / length;
    const double xx = nx*nx, yy = ny*ny, zz = nz*nz;
    const double basis[16]{0.2820947917738781, -0.4886025119029199*ny, 0.4886025119029199*nz, -0.4886025119029199*nx,
      1.0925484305920792*nx*ny, -1.0925484305920792*ny*nz, 0.31539156525252005*(3*zz-1),
      -1.0925484305920792*nx*nz, 0.5462742152960396*(xx-yy),
      -0.5900435899266435*ny*(3*xx-yy), 2.890611442640554*nx*ny*nz, -0.4570457994644658*ny*(5*zz-1),
      0.3731763325901154*nz*(5*zz-3), -0.4570457994644658*nx*(5*zz-1),
      1.445305721320277*nz*(xx-yy), -0.5900435899266435*nx*(xx-3*yy)};
    Vector color{0.5, 0.5, 0.5};
    for (int i = 0; i < (scene.sh_degree+1)*(scene.sh_degree+1); ++i)
      for (size_t channel = 0; channel < 3; ++channel) color[channel] += basis[i] * p.sh[static_cast<size_t>(i)*3+channel];
    for (auto& channel : color) channel = std::max(0.0, channel);
    points.push_back({camera.fx*x + camera.cx, camera.fy*y + camera.cy, c/det, -b/det, a/det, p.center_opacity[3], z, color});
  }
  std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
  QVariantList samples;
  double native_squared = 0, trainer_squared = 0;
  int maximum_native = 0, maximum_trainer = 0;
  for (int row = 0; row < 12; ++row) for (int column = 0; column < 16; ++column) {
    const int px = (2*column+1)*camera.image_size.width()/32;
    const int py = (2*row+1)*camera.image_size.height()/24;
    Vector rgb{};
    double transmittance = 1;
    for (const auto& point : points) {
      const double dx = px + 0.5 - point.x, dy = py + 0.5 - point.y;
      const double power = 0.5 * (point.a*dx*dx + 2*point.b*dx*dy + point.c*dy*dy);
      if (power < 0 || power > std::log(255*point.alpha)) continue;
      const double alpha = std::min(0.999, point.alpha * std::exp(-power));
      if (alpha < 1.0 / 255.0) continue;
      const double next = transmittance * (1-alpha);
      if (next <= 1e-4) break;
      for (size_t channel = 0; channel < 3; ++channel) rgb[channel] += transmittance*alpha*point.color[channel];
      transmittance = next;
    }
    QVariantList expected, actual, reference;
    const auto native_color = native.pixelColor(px, py), trainer_color = trainer.pixelColor(px, py);
    const int native_rgb[]{native_color.red(), native_color.green(), native_color.blue()};
    const int trainer_rgb[]{trainer_color.red(), trainer_color.green(), trainer_color.blue()};
    int sample_native_max = 0, sample_trainer_max = 0;
    for (size_t channel = 0; channel < 3; ++channel) {
      const int quantized = static_cast<int>(std::lround(std::clamp(rgb[channel], 0.0, 1.0)*255));
      const int dn = native_rgb[channel] - quantized, dt = trainer_rgb[channel] - quantized;
      native_squared += dn*dn; trainer_squared += dt*dt;
      sample_native_max = std::max(sample_native_max, std::abs(dn)); sample_trainer_max = std::max(sample_trainer_max, std::abs(dt));
      expected.append(quantized); actual.append(native_rgb[channel]); reference.append(trainer_rgb[channel]);
    }
    maximum_native = std::max(maximum_native, sample_native_max); maximum_trainer = std::max(maximum_trainer, sample_trainer_max);
    if (sample_native_max > 1 || sample_trainer_max > 1)
      samples.append(QVariantMap{{"x", px}, {"y", py}, {"scalarRgb", expected}, {"nativeRgb", actual}, {"trainerRgb", reference}});
  }
  return {{"sampleCount", 192}, {"nativeMaxChannelDifference", maximum_native}, {"trainerMaxChannelDifference", maximum_trainer},
      {"nativePsnr", 10*std::log10(255.0*255.0/(std::max(native_squared, 1e-12)/576))},
      {"trainerPsnr", 10*std::log10(255.0*255.0/(std::max(trainer_squared, 1e-12)/576))}, {"differingSamples", samples}};
}
}
