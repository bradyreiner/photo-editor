#include "photo_text_dimension_mesh.h"

#include <QColorSpace>
#include <QGuiApplication>
#include <QPolygonF>

#include <cmath>
#include <iostream>

using namespace photo_editor::desktop;

namespace {

double triangle_area_xy(const PhotoTextDimensionTriangle& triangle) {
  const auto& a = triangle.vertices[0].position;
  const auto& b = triangle.vertices[1].position;
  const auto& c = triangle.vertices[2].position;
  const auto cross = QVector3D::crossProduct(b - a, c - a);
  return std::abs(static_cast<double>(cross.z())) * 0.5;
}

double alpha_at(const QImage& image, int x, int y) {
  const auto* rgba = reinterpret_cast<const float*>(image.constScanLine(y));
  return rgba[static_cast<std::size_t>(x) * 4U + 3U];
}

double red_at(const QImage& image, int x, int y) {
  const auto* rgba = reinterpret_cast<const float*>(image.constScanLine(y));
  return rgba[static_cast<std::size_t>(x) * 4U];
}

bool has_face(const PhotoTextDimensionMesh& mesh, PhotoTextDimensionFace face) {
  for (const auto& triangle : mesh.triangles)
    if (triangle.face == face) return true;
  return false;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);

  QPainterPath donut;
  donut.setFillRule(Qt::OddEvenFill);
  donut.addRect(QRectF(0.0, 0.0, 100.0, 100.0));
  donut.addRect(QRectF(30.0, 30.0, 40.0, 40.0));

  PhotoTextDimensionGeometryOptions geometry_options;
  geometry_options.depth = 20.0;
  geometry_options.front_color = QColor(255, 255, 255, 128);
  const auto mesh = build_photo_text_dimension_mesh(donut, geometry_options);
  if (!mesh.is_valid()) return fail("A donut outline should create real 3D geometry.");
  if (std::abs(mesh.lower.z() + 20.0F) > 1.0e-4F || std::abs(mesh.upper.z()) > 1.0e-4F ||
      !has_face(mesh, PhotoTextDimensionFace::Front) || !has_face(mesh, PhotoTextDimensionFace::Back) ||
      !has_face(mesh, PhotoTextDimensionFace::Side)) {
    return fail("The mesh must include front, back, side, and full extrusion depth.");
  }
  double front_area = 0.0;
  for (const auto& triangle : mesh.triangles)
    if (triangle.face == PhotoTextDimensionFace::Front) front_area += triangle_area_xy(triangle);
  if (std::abs(front_area - 8400.0) > 0.1) {
    return fail("Scanline cap tessellation must preserve the counter hole area.");
  }

  PhotoTextDimensionGeometryOptions bevel_options = geometry_options;
  bevel_options.bevel_width = 4.0;
  bevel_options.bevel_segments = 32;
  if (!build_photo_text_dimension_mesh(donut, bevel_options).is_valid())
    return fail("The full versioned bevel segment range must render within the triangle budget.");
  bevel_options.bevel_segments = 4;
  bevel_options.bevel_profile = PhotoTextDimensionBevelProfile::Rounded;
  const auto beveled = build_photo_text_dimension_mesh(donut, bevel_options);
  if (!beveled.is_valid() || !has_face(beveled, PhotoTextDimensionFace::Bevel)) {
    return fail("A positive bevel must create a segmented bevel surface.");
  }
  PhotoTextDimensionGeometryOptions flat_options = geometry_options;
  flat_options.depth = 0.0;
  const auto flat = build_photo_text_dimension_mesh(donut, flat_options);
  double flat_area = 0.0;
  for (const auto& triangle : flat.triangles)
    if (triangle.face == PhotoTextDimensionFace::Front) flat_area += triangle_area_xy(triangle);
  if (!flat.is_valid() || std::abs(flat_area - 8400.0) > 0.1 ||
      std::abs(flat.lower.z()) > 1.0e-4F || std::abs(flat.upper.z()) > 1.0e-4F ||
      has_face(flat, PhotoTextDimensionFace::Back)) {
    return fail("Zero extrusion must stay a valid flat, hole-preserving outline.");
  }

  QPainterPath combined = donut;
  QPainterPath concave;
  concave.setFillRule(Qt::WindingFill);
  QPolygonF l_shape;
  l_shape << QPointF(120.0, 0.0) << QPointF(160.0, 0.0) << QPointF(160.0, 20.0)
          << QPointF(140.0, 20.0) << QPointF(140.0, 40.0) << QPointF(120.0, 40.0);
  concave.addPolygon(l_shape);
  combined.addPath(concave);
  const auto concave_mesh = build_photo_text_dimension_mesh(combined, geometry_options);
  if (!concave_mesh.is_valid()) return fail("Concave and multiple outlines must tessellate.");
  double combined_front_area = 0.0;
  for (const auto& triangle : concave_mesh.triangles)
    if (triangle.face == PhotoTextDimensionFace::Front) combined_front_area += triangle_area_xy(triangle);
  if (std::abs(combined_front_area - 9600.0) > 0.1) {
    return fail("Concave and disconnected outline regions must preserve their filled area.");
  }

  PhotoTextDimensionCamera camera;
  camera.pivot = QVector3D(50.0F, 50.0F, 0.0F);
  camera.screen_origin = QPointF(50.3, 50.3);
  camera.projection = PhotoTextDimensionProjection::Perspective;
  camera.camera_distance = 500.0;
  camera.perspective_focal_length_pixels = 500.0;
  camera.near_plane = 0.1;
  bool point_valid = false;
  const QPointF pivot_projection = project_photo_text_dimension_point(
      QVector3D(50.0F, 50.0F, 0.0F), camera, {}, &point_valid);
  if (!point_valid || std::abs(pivot_projection.x() - 50.3) > 1.0e-5 ||
      std::abs(pivot_projection.y() - 50.3) > 1.0e-5) {
    return fail("The camera pivot should remain at its screen origin under perspective.");
  }
  camera.rotation = QQuaternion::fromAxisAndAngle(QVector3D(0.0F, 1.0F, 0.0F), 37.0F);
  const QPointF rotated_pivot_projection = project_photo_text_dimension_point(
      QVector3D(50.0F, 50.0F, 0.0F), camera, {}, &point_valid);
  if (!point_valid || std::abs(rotated_pivot_projection.x() - 50.3) > 1.0e-5 ||
      std::abs(rotated_pivot_projection.y() - 50.3) > 1.0e-5) {
    return fail("Camera rotation must remain centered on the authored pivot.");
  }
  camera.rotation = QQuaternion();
  PhotoTextDimensionCamera too_close = camera;
  too_close.camera_distance = 0.05;
  const auto too_close_projection = project_photo_text_dimension_point(
      QVector3D(50.0F, 50.0F, 0.0F), too_close, {}, &point_valid);
  (void)too_close_projection;
  if (point_valid) {
    return fail("Points behind the camera near plane must be rejected.");
  }
  PhotoTextDimensionMesh partial_near_mesh;
  PhotoTextDimensionTriangle partial_near_triangle;
  partial_near_triangle.vertices = {{{QVector3D(1.0F, 1.0F, 0.0F), QVector3D(0, 0, 1)},
                                     {QVector3D(9.0F, 1.0F, 0.0F), QVector3D(0, 0, 1)},
                                     {QVector3D(5.0F, 9.0F, 99.95F), QVector3D(0, 0, 1)}}};
  partial_near_mesh.triangles.push_back(partial_near_triangle);
  PhotoTextDimensionCamera partial_near_camera;
  partial_near_camera.camera_distance = 100.0;
  const auto partial_near = preflight_photo_text_dimension_mesh(
      partial_near_mesh, partial_near_camera,
      PhotoTextDimensionRenderOptions{.reference_rect = QRectF(0, 0, 10, 10)});
  if (partial_near.is_valid() || !partial_near.error.contains(QStringLiteral("near plane"))) {
    return fail("Preflight must reject an entire mesh when any triangle crosses the near plane.");
  }

  PhotoTextDimensionRenderOptions render_options;
  render_options.reference_rect = QRectF(0.0, 0.0, 100.0, 100.0);
  render_options.antialias_samples = 2;
  const auto preflight = preflight_photo_text_dimension_mesh(mesh, camera, render_options);
  PhotoTextDimensionRenderStyle hdr_style;
  hdr_style.ambient = 2.0;
  hdr_style.key_light = 0.0;
  const auto rendered = render_photo_text_dimension_mesh(mesh, camera, hdr_style, render_options);
  if (!preflight.is_valid() || !rendered.is_valid() ||
      rendered.image.format() != QImage::Format_RGBA32FPx4 ||
      rendered.image.colorSpace() != QColorSpace(QColorSpace::SRgbLinear)) {
    return fail("Preflight and rendering must return a linear float image with explicit bounds.");
  }
  if (rendered.pixel_bounds != preflight.pixel_bounds || rendered.origin_offset != preflight.origin_offset ||
      std::abs((rendered.origin_offset.x() + rendered.pixel_bounds.width() * 0.5) - 50.0) > 1.0e-9 ||
      std::abs((rendered.origin_offset.y() + rendered.pixel_bounds.height() * 0.5) - 50.0) > 1.0e-9) {
    return fail("Expanded symmetric padding must preserve the authored layer midpoint.");
  }
  const int hole_x = static_cast<int>(std::floor(50.3 - rendered.origin_offset.x()));
  const int hole_y = static_cast<int>(std::floor(50.3 - rendered.origin_offset.y()));
  if (alpha_at(rendered.image, hole_x, hole_y) != 0.0) {
    return fail("The counter in a glyph outline must stay transparent after rendering.");
  }
  const int interior_x = static_cast<int>(std::floor(10.3 - rendered.origin_offset.x()));
  const int interior_y = static_cast<int>(std::floor(10.3 - rendered.origin_offset.y()));
  if (std::abs(alpha_at(rendered.image, interior_x, interior_y) - 128.0 / 255.0) > 0.01 ||
      red_at(rendered.image, interior_x, interior_y) <= 1.0F) {
    return fail("Linear float shading must preserve HDR headroom and fractional material alpha.");
  }
  bool has_fractional_edge_alpha = false;
  for (int y = 0; y < rendered.image.height(); ++y) {
    for (int x = 0; x < rendered.image.width(); ++x) {
      const double alpha = alpha_at(rendered.image, x, y);
      if (alpha > 0.0 && alpha < 128.0 / 255.0 - 0.01) has_fractional_edge_alpha = true;
    }
  }
  if (!has_fractional_edge_alpha) return fail("Supersampled transparent edges must retain fractional coverage.");

  PhotoTextDimensionMesh overlapping;
  const auto make_triangle = [](float z, QColor color) {
    PhotoTextDimensionTriangle triangle;
    triangle.face = PhotoTextDimensionFace::Front;
    triangle.front_color = color;
    triangle.vertices = {{{QVector3D(1.0F, 1.0F, z), QVector3D(0, 0, 1)},
                          {QVector3D(9.0F, 1.0F, z), QVector3D(0, 0, 1)},
                          {QVector3D(5.0F, 9.0F, z), QVector3D(0, 0, 1)}}};
    return triangle;
  };
  overlapping.triangles.push_back(make_triangle(0.0F, QColor(255, 0, 0)));
  overlapping.triangles.push_back(make_triangle(-10.0F, QColor(0, 0, 255)));
  PhotoTextDimensionCamera z_camera;
  z_camera.pivot = QVector3D(5.0F, 5.0F, 0.0F);
  z_camera.screen_origin = QPointF(5.0, 5.0);
  z_camera.camera_distance = 100.0;
  PhotoTextDimensionRenderOptions z_options;
  z_options.reference_rect = QRectF(0.0, 0.0, 10.0, 10.0);
  z_options.antialias_samples = 1;
  PhotoTextDimensionRenderStyle z_style;
  z_style.ambient = 1.0;
  z_style.key_light = 0.0;
  const auto depth_tested = render_photo_text_dimension_mesh(overlapping, z_camera, z_style, z_options);
  if (!depth_tested.is_valid()) return fail("A two-layer z-buffer fixture should render.");
  const int overlap_x = static_cast<int>(std::floor(5.0 - depth_tested.origin_offset.x()));
  const int overlap_y = static_cast<int>(std::floor(4.0 - depth_tested.origin_offset.y()));
  const auto* overlap_pixel = reinterpret_cast<const float*>(depth_tested.image.constScanLine(overlap_y)) + overlap_x * 4;
  if (overlap_pixel[0] <= overlap_pixel[2]) {
    return fail("The nearest triangle must win the shared z-buffer regardless of mesh order.");
  }

  PhotoTextDimensionRenderOptions tiny_budget = render_options;
  tiny_budget.max_working_bytes = 1024;
  if (preflight_photo_text_dimension_mesh(mesh, camera, tiny_budget).is_valid()) {
    return fail("Render preflight must reject output before exceeding its memory budget.");
  }
  PhotoTextDimensionRenderOptions triangle_budget = render_options;
  triangle_budget.max_triangles = 1;
  if (preflight_photo_text_dimension_mesh(mesh, camera, triangle_budget).is_valid()) {
    return fail("Render preflight must reject combined glyph meshes before exceeding the triangle budget.");
  }
  return 0;
}
