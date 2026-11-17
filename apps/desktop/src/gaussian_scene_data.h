#pragma once
#include "gaussian_scene_limits.h"
#include "gaussian_photo_settings.h"
#include "gaussian_physical_camera_settings.h"
#include "gaussian_scene_composition.h"
#include <QMatrix4x4>
#include <QSize>
#include <QVariantMap>
#include <QVector3D>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace photo_editor::desktop {
// Interleaved RGB SH coefficients, with explicit 16-byte layout for Metal.
struct alignas(16) GaussianPoint {
  std::array<float, 4> center_opacity{};
  std::array<float, 4> covariance_a{};  // xx, xy, xz, yy
  std::array<float, 4> covariance_b{};  // yz, zz, padding
  std::array<float, 48> sh{};
};
static_assert(sizeof(GaussianPoint) == 240);
// An exact conditional of a full 4D Gaussian: the corresponding point holds
// mean XYZ, peak opacity and conditional XYZ covariance. Time is local to the
// scene origin; velocity is covariance(XYZ,t) / variance(t).
struct alignas(16) GaussianTemporalPoint {
  std::array<float, 4> velocity_time{};  // vx, vy, vz, mean local seconds
  std::array<float, 4> normalization{};  // inverse temporal sigma, padding
  // Column-major square root F of conditional covariance (F F^T), padded
  // float4 columns. Projecting F avoids losing thin axes in float covariance.
  std::array<float, 12> covariance_factor{};
};
static_assert(sizeof(GaussianTemporalPoint) == 80);
struct alignas(16) GaussianTemporalAppearance {
  // Two cosine temporal orders, each with 16 RGB spatial SH coefficients.
  std::array<float, 96> sh{};
};
static_assert(sizeof(GaussianTemporalAppearance) == 384);
struct GaussianCamera {
  QString name;
  QSize image_size;
  float fx = 1, fy = 1, cx = 0, cy = 0;
  QVector3D position, right{1, 0, 0}, down{0, 1, 0}, forward{0, 0, 1};
  GaussianPhotoSettings photo;
  std::optional<GaussianPhysicalCalibration> physical;
  // Scene-wide immutable composition, supplied by the viewport/export owner.
  // Kept separate from camera persistence: recalling a shot does not roll back geometry.
  std::shared_ptr<const GaussianSceneComposition> composition;
  QMatrix4x4 worldToCamera() const;
  GaussianCamera resized(QSize size) const;
  QVariantMap description() const;
};
struct GaussianSceneData {
  std::vector<GaussianPoint> points;
  // Original static square roots (column-major, padded columns). Keep thin
  // axes which may disappear when the convenience covariance rounds to float.
  // Empty for synthetic covariance-only scenes and temporal scenes.
  std::vector<std::array<float,12>> spatial_factors;
  std::vector<GaussianTemporalPoint> temporal;  // Empty for static 3D.
  std::vector<GaussianTemporalAppearance> temporal_appearance;
  double time_origin_seconds = 0;
  std::array<float, 2> local_time_range{0, 1};
  int temporal_sh_degree = 0;  // 0–2; temporal period is the local time span.
  std::vector<GaussianCamera> cameras;
  QString artifact_sha256;
  int sh_degree = 0;
  float focus_distance = 1;
  std::shared_ptr<const GaussianSceneData> cleanup_source;
  QByteArray cleanup_mask;
  QVariantMap cleanup_receipt;
};
inline GaussianAlphaModel gaussian_alpha_model(const GaussianCamera& camera) {
  return camera.composition ? camera.composition->alpha_model : GaussianAlphaModel::Brush999;
}
struct GaussianSceneLoad {
  std::shared_ptr<const GaussianSceneData> scene;
  QString error;
  GaussianCompositionLoad composition{};
};
GaussianSceneLoad load_gaussian_scene(const QString& artifact, const QString& camera_directory,
    const QString& expected_sha256, const std::shared_ptr<std::atomic_bool>& canceled = {});
GaussianCamera navigate_gaussian_camera(const GaussianCamera& base, float focus, float yaw_degrees,
    float pitch_degrees, float dolly, float pan_x, float pan_y);
GaussianCamera navigate_gaussian_camera_around(const GaussianCamera& base, QVector3D pivot,
    float yaw_degrees, float pitch_degrees, float dolly, float pan_x, float pan_y);
QVector3D gaussian_camera_pan_translation(const GaussianCamera& camera, float focus_depth,
    double normalized_viewport_dx, double normalized_viewport_dy);
}  // namespace photo_editor::desktop
