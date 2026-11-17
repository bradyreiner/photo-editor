#pragma once
#include "gaussian_scene_data.h"
#include <QPointF>
#include <QRect>
#include <algorithm>
#include <limits>

namespace photo_editor::desktop {
// Creative analytic lens model, not a fitted glass prescription. A bounded
// radial projection remains invertible even outside the authored frame.
inline QPointF gaussian_lens_field(const GaussianCamera& c, QPointF pixel) {
  return {(pixel.x()-c.cx)/(c.fx*c.photo.focal_scale),(pixel.y()-c.cy)/(c.fy*c.photo.focal_scale)};
}
inline double gaussian_lens_field_weight(QPointF q) { const double r2 = q.x()*q.x()+q.y()*q.y(); return r2/(1+r2); }
inline QPointF gaussian_lens_chief_pixel(const GaussianCamera& c, QPointF pixel) {
  const auto q = gaussian_lens_field(c,pixel);
  const double scale = 1+c.photo.lens.spherical_distortion*gaussian_lens_field_weight(q);
  return {c.cx+c.fx*c.photo.focal_scale*q.x()*scale,c.cy+c.fy*c.photo.focal_scale*q.y()*scale};
}
inline QPointF gaussian_lens_display_pixel(const GaussianCamera& c, QPointF chief) {
  if (c.photo.lens.spherical_distortion == 0) return chief;
  const auto q = gaussian_lens_field(c,chief); const double r = std::hypot(q.x(),q.y());
  if (r == 0) return chief;
  const double k = c.photo.lens.spherical_distortion;
  double low = r/(1+std::abs(k)), high = r/(1-std::abs(k));
  for (int i = 0; i < 48; ++i) {
    const double x = (low+high)*0.5;
    if (x*(1+k*x*x/(1+x*x)) < r) low = x; else high = x;
  }
  const double scale = (low+high)*0.5/r;
  return {c.cx+c.fx*c.photo.focal_scale*q.x()*scale,c.cy+c.fy*c.photo.focal_scale*q.y()*scale};
}
inline QPointF gaussian_lens_sample_pixel(const GaussianCamera& c, QPointF pixel, QPointF aperture) {
  const auto q = gaussian_lens_field(c,pixel); const double b = gaussian_lens_field_weight(q);
  const double r2 = q.x()*q.x()+q.y()*q.y();
  const double dot = q.x()*aperture.x()+q.y()*aperture.y();
  const QPointF astig = r2 > 0 ? QPointF(2*q.x()*dot/r2-aperture.x(),2*q.y()*dot/r2-aperture.y()) : QPointF{};
  const auto& lens = c.photo.lens;
  const auto chief = gaussian_lens_chief_pixel(c,pixel);
  const double curvature = lens.field_curvature+lens.spherical_distortion;
  return {chief.x()-c.fx*c.photo.focal_scale*b*(curvature*aperture.x()+lens.astigmatism*astig.x())/lens.focus_distance,
      chief.y()-c.fy*c.photo.focal_scale*b*(curvature*aperture.y()+lens.astigmatism*astig.y())/lens.focus_distance};
}
// Conservative whole-pupil bounds: interval radial projection plus the operator
// norm of the curvature/astigmatism matrices. Integer-aligned source grids keep
// bilinear samples identical across tiles and quality prefixes.
inline std::optional<QRect> gaussian_lens_source_region(const GaussianCamera& c, const QRect& output, bool aperture) {
  const auto& lens = c.photo.lens;
  if (lens.spherical_distortion == 0 && (!aperture || (lens.astigmatism == 0 && lens.field_curvature == 0))) return output;
  const auto a = gaussian_lens_field(c,QPointF(output.left(),output.top()));
  const auto z = gaussian_lens_field(c,QPointF(output.left()+output.width(),output.top()+output.height()));
  const double x = std::max(std::abs(a.x()),std::abs(z.x())), y = std::max(std::abs(a.y()),std::abs(z.y()));
  const double b = (x*x+y*y)/(1+x*x+y*y), s0 = std::min(1.0,1+lens.spherical_distortion*b), s1 = std::max(1.0,1+lens.spherical_distortion*b);
  const double pupil_extent = std::sqrt(std::max(lens.bokeh_aspect,1/lens.bokeh_aspect));
  const double extra = aperture ? pupil_extent*lens.aperture_radius/lens.focus_distance*(std::abs(lens.field_curvature+lens.spherical_distortion)+std::abs(lens.astigmatism))*b : 0;
  const auto interval = [&](double lo,double hi,double focal,double principal) {
    return std::pair{std::floor(principal+focal*(std::min({lo*s0,lo*s1,hi*s0,hi*s1})-extra)-2),
        std::ceil(principal+focal*(std::max({lo*s0,lo*s1,hi*s0,hi*s1})+extra)+2)};
  };
  const auto bx = interval(a.x(),z.x(),c.fx*c.photo.focal_scale,c.cx), by = interval(a.y(),z.y(),c.fy*c.photo.focal_scale,c.cy);
  for (const double v : {bx.first,bx.second,by.first,by.second})
    if (!std::isfinite(v) || v <= std::numeric_limits<int>::min()/2.0 || v >= std::numeric_limits<int>::max()/2.0) return {};
  return QRect(static_cast<int>(bx.first),static_cast<int>(by.first),static_cast<int>(bx.second-bx.first),static_cast<int>(by.second-by.first));
}
}  // namespace photo_editor::desktop
