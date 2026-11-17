#pragma once
#include "gaussian_spacetime.h"
#include <functional>
#include <limits>

namespace photo_editor::desktop {
// Secondary visibility uses the maximum anisotropic Gaussian response over a
// finite ray segment, followed by product transmittance. This is an appearance
// opacity model, not a calibrated extinction integral. No screen-space floor.
inline constexpr float gaussian_scan_cutoff = 1.0F/255.0F;
struct alignas(16) GaussianScanParticle {
  std::array<float,4> center_opacity{}, row0{}, row1{}, row2{}; // inverse covariance square root
};
struct alignas(16) GaussianScanNode {
  std::array<float,4> lower{}, upper{};
  std::array<quint32,4> links{}; // first particle, count (zero for branch), escape, unused
};
static_assert(sizeof(GaussianScanParticle) == 64 && sizeof(GaussianScanNode) == 48);
struct GaussianScanIndex {
  std::vector<GaussianScanParticle> particles;
  std::vector<GaussianScanNode> nodes;
  std::vector<std::array<float,6>> bounds; // Packed lower/upper XYZ for Metal's native builder.
  QString error;
  bool canceled = false;
};
inline GaussianScanIndex build_gaussian_scan_index(const GaussianSceneData& scene, float local_seconds,
    const std::shared_ptr<std::atomic_bool>& canceled = {}, bool native_bounds = false) {
  GaussianScanIndex result;
  const auto stopping = [&] { return canceled && canceled->load(); };
  if (scene.points.size() > gaussian_static_point_limit || (!scene.temporal.empty() && scene.temporal.size() != scene.points.size()) ||
      (!scene.spatial_factors.empty() && scene.spatial_factors.size() != scene.points.size()) || !std::isfinite(local_seconds)) {
    result.error = "Invalid Gaussian scan visibility input."; return result;
  }
  struct Entry { GaussianScanParticle particle; std::array<float,3> lower{}, upper{}; };
  std::vector<Entry> entries; entries.reserve(scene.points.size());
  for (size_t i = 0; i < scene.points.size(); ++i) {
    if ((i & 4095U) == 0 && stopping()) { result.canceled = true; return result; }
    const auto& point = scene.points[i];
    Entry entry; entry.particle.center_opacity = point.center_opacity;
    entry.particle.row2[3] = static_cast<float>(i); // Exact source index below the two-million-point admission cap.
    double factor[3][3]{};
    if (!scene.temporal.empty()) {
      const auto& motion = scene.temporal[i];
      const float dt = local_seconds-motion.velocity_time[3], normalized = dt*motion.normalization[0];
      entry.particle.center_opacity[3] *= std::exp(-0.5F*normalized*normalized);
      const auto center = gaussian_spacetime_center(point,motion,local_seconds);
      for (size_t a = 0; a < 3; ++a) entry.particle.center_opacity[a] = center[a];
      for (size_t a = 0; a < 3; ++a) for (size_t b = 0; b < 3; ++b) factor[a][b] = motion.covariance_factor[b*4+a];
    } else if (!scene.spatial_factors.empty()) {
      for (size_t a = 0; a < 3; ++a) for (size_t b = 0; b < 3; ++b) factor[a][b] = scene.spatial_factors[i][b*4+a];
    } else {
      const auto& a = point.covariance_a; const auto& b = point.covariance_b;
      const double covariance[3][3]{{a[0],a[1],a[2]},{a[1],a[3],b[0]},{a[2],b[0],b[1]}};
      for (size_t row = 0; row < 3; ++row) for (size_t col = 0; col <= row; ++col) {
        double residual = covariance[row][col];
        for (size_t k = 0; k < col; ++k) residual -= factor[row][k]*factor[col][k];
        if (!std::isfinite(residual) || (row == col && residual <= 0)) {
          result.error = "Scan shadows require positive definite Gaussian covariances."; return result;
        }
        factor[row][col] = row == col ? std::sqrt(residual) : residual/factor[col][col];
      }
    }
    for (const auto v : entry.particle.center_opacity) if (!std::isfinite(v)) { result.error = "Invalid scan shadow position or opacity."; return result; }
    const double opacity = entry.particle.center_opacity[3];
    if (opacity < 0 || opacity > 1) { result.error = "Invalid scan shadow opacity."; return result; }
    if (opacity < gaussian_scan_cutoff) continue;
    const double determinant = factor[0][0]*(factor[1][1]*factor[2][2]-factor[1][2]*factor[2][1])-
        factor[0][1]*(factor[1][0]*factor[2][2]-factor[1][2]*factor[2][0])+
        factor[0][2]*(factor[1][0]*factor[2][1]-factor[1][1]*factor[2][0]);
    if (!std::isfinite(determinant) || determinant == 0) { result.error = "Invalid scan shadow covariance factor."; return result; }
    std::array<float,4>* rows[]{&entry.particle.row0,&entry.particle.row1,&entry.particle.row2};
    const double radius = std::sqrt(2*std::log(opacity/gaussian_scan_cutoff));
    for (size_t a = 0; a < 3; ++a) {
      const double extent = radius*std::hypot(factor[a][0],factor[a][1],factor[a][2]);
      const double center = entry.particle.center_opacity[a];
      // Round bounds outward; avoid rejecting a threshold response on a face.
      entry.lower[a] = std::nextafter(static_cast<float>(center-extent),-std::numeric_limits<float>::infinity());
      entry.upper[a] = std::nextafter(static_cast<float>(center+extent),std::numeric_limits<float>::infinity());
      if (!std::isfinite(entry.lower[a]) || !std::isfinite(entry.upper[a])) { result.error = "Scan shadow bounds exceed float range."; return result; }
      for (size_t b = 0; b < 3; ++b) {
        const size_t r = (b+1)%3, s = (b+2)%3, c = (a+1)%3, d = (a+2)%3;
        (*rows[a])[b] = static_cast<float>((factor[r][c]*factor[s][d]-factor[r][d]*factor[s][c])/determinant);
        if (!std::isfinite((*rows[a])[b])) { result.error = "Scan shadow precision exceeds float range."; return result; }
      }
    }
    entries.push_back(entry);
  }
  if (!native_bounds) result.nodes.reserve(entries.size()/2+1);
  const auto build = [&](auto&& self,size_t first,size_t last) -> void {
    if (stopping()) { result.canceled = true; return; }
    const auto index = result.nodes.size(); result.nodes.emplace_back();
    GaussianScanNode node;
    std::array<float,3> centroid_low, centroid_high;
    centroid_low.fill(std::numeric_limits<float>::infinity()); centroid_high.fill(-std::numeric_limits<float>::infinity());
    node.lower.fill(std::numeric_limits<float>::infinity()); node.upper.fill(-std::numeric_limits<float>::infinity());
    for (size_t i = first; i < last; ++i) for (size_t axis = 0; axis < 3; ++axis) {
      centroid_low[axis] = std::min(centroid_low[axis],entries[i].particle.center_opacity[axis]);
      centroid_high[axis] = std::max(centroid_high[axis],entries[i].particle.center_opacity[axis]);
      node.lower[axis] = std::min(node.lower[axis],entries[i].lower[axis]); node.upper[axis] = std::max(node.upper[axis],entries[i].upper[axis]);
    }
    if (last-first <= 8) node.links = {static_cast<quint32>(first),static_cast<quint32>(last-first),0,0};
    else {
      size_t axis = 0; for (size_t a = 1; a < 3; ++a) if (centroid_high[a]-centroid_low[a] > centroid_high[axis]-centroid_low[axis]) axis = a;
      const auto middle = first+(last-first)/2;
      std::nth_element(entries.begin()+static_cast<ptrdiff_t>(first),entries.begin()+static_cast<ptrdiff_t>(middle),entries.begin()+static_cast<ptrdiff_t>(last),
          [axis](const Entry& a,const Entry& b) { return a.particle.center_opacity[axis] < b.particle.center_opacity[axis]; });
      self(self,first,middle); if (!result.canceled) self(self,middle,last);
    }
    node.links[2] = static_cast<quint32>(result.nodes.size()); result.nodes[index] = node;
  };
  if (!native_bounds && !entries.empty()) build(build,0,entries.size());
  if (result.canceled) { result.nodes.clear(); return result; }
  result.particles.reserve(entries.size());
  if (native_bounds) result.bounds.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    if ((i & 4095U) == 0 && stopping()) { result.canceled = true; result.particles.clear(); result.nodes.clear(); return result; }
    result.particles.push_back(entries[i].particle);
    if (native_bounds) result.bounds.push_back({entries[i].lower[0],entries[i].lower[1],entries[i].lower[2],entries[i].upper[0],entries[i].upper[1],entries[i].upper[2]});
  }
  return result;
}
}  // namespace photo_editor::desktop
