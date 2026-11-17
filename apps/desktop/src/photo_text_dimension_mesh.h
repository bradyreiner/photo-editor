#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QQuaternion>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector3D>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace photo_editor::desktop {

enum class PhotoTextDimensionFace : std::uint8_t {
  Front,
  Back,
  Side,
  Bevel,
};

enum class PhotoTextDimensionBevelProfile : std::uint8_t {
  Rounded,
  Chamfer,
};

struct PhotoTextDimensionVertex {
  QVector3D position;
  QVector3D normal;
};

struct PhotoTextDimensionTriangle {
  std::array<PhotoTextDimensionVertex, 3> vertices;
  PhotoTextDimensionFace face = PhotoTextDimensionFace::Front;
  // An invalid color uses the render style's front material. A valid value
  // keeps per-glyph colors when multiple glyph meshes share one z-buffer.
  QColor front_color;
};

struct PhotoTextDimensionMesh {
  std::vector<PhotoTextDimensionTriangle> triangles;
  QVector3D lower;
  QVector3D upper;
  QString error;

  [[nodiscard]] bool is_valid() const { return error.isEmpty() && !triangles.empty(); }
};

struct PhotoTextDimensionGeometryOptions {
  double depth = 24.0;
  double bevel_width = 0.0;
  PhotoTextDimensionBevelProfile bevel_profile = PhotoTextDimensionBevelProfile::Rounded;
  int bevel_segments = 3;
  double flatten_tolerance = 0.25;
  QColor front_color;
  std::size_t max_path_elements = 200000;
  std::size_t max_contours = 20000;
  std::size_t max_edges = 100000;
  std::size_t max_triangles = 500000;
  std::function<bool()> canceled;
};

// Converts the filled QPainterPath (including non-zero and even-odd holes)
// into front/back caps, contour walls, and a bounded, segmented bevel profile.
// Curves are adaptively flattened; caps use a scanline slab tessellator, never
// a contour fan, so concave outlines and counters remain open.
[[nodiscard]] PhotoTextDimensionMesh build_photo_text_dimension_mesh(
    const QPainterPath& path,
    const PhotoTextDimensionGeometryOptions& options = {});

enum class PhotoTextDimensionProjection : std::uint8_t {
  Orthographic,
  Perspective,
};

struct PhotoTextDimensionCamera {
  PhotoTextDimensionProjection projection = PhotoTextDimensionProjection::Orthographic;
  QVector3D pivot;
  QQuaternion rotation;
  QPointF screen_origin;
  double orthographic_pixels_per_unit = 1.0;
  double perspective_focal_length_pixels = 720.0;
  double camera_distance = 1000.0;
  double near_plane = 0.1;
};

struct PhotoTextDimensionMaterial {
  QColor color = QColor(240, 240, 240, 255);
  double roughness = 0.55;
  double metallic = 0.0;
};

struct PhotoTextDimensionRenderStyle {
  PhotoTextDimensionMaterial front;
  PhotoTextDimensionMaterial back = {QColor(210, 210, 210, 255), 0.65, 0.0};
  PhotoTextDimensionMaterial side = {QColor(175, 175, 175, 255), 0.48, 0.0};
  PhotoTextDimensionMaterial bevel = {QColor(230, 230, 230, 255), 0.35, 0.0};
  QVector3D light_direction = QVector3D(-0.38F, -0.48F, 0.79F);
  double ambient = 0.28;
  double key_light = 0.78;
  double specular_intensity = 0.22;
};

struct PhotoTextDimensionRenderOptions {
  // Logical authored canvas coordinates are projected first, then scaled per
  // axis to raster pixels. This lets the caller preserve anisotropic output
  // scaling without changing the 3D geometry or camera.
  QPointF output_scale = QPointF(1.0, 1.0);
  QRectF reference_rect;
  bool symmetric_padding = true;
  int antialias_samples = 2;
  int max_dimension = 16384;
  std::size_t max_triangles = 500000;
  std::uint64_t max_output_pixels = 16000000;
  std::uint64_t max_working_samples = 12000000;
  std::uint64_t max_working_bytes = 256ULL * 1024ULL * 1024ULL;
  std::function<bool()> canceled;
};

struct PhotoTextDimensionProjectionResult {
  QRect pixel_bounds;
  QPoint origin_offset;
  QString error;

  [[nodiscard]] bool is_valid() const { return error.isEmpty() && pixel_bounds.isValid(); }
};

// Projects an authored-space point with the same camera math used by the CPU
// renderer. `valid` is false when the point is non-finite or behind near_plane.
[[nodiscard]] QPointF project_photo_text_dimension_point(
    QVector3D point,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderOptions& options,
    bool* valid = nullptr);

// Computes the complete expanded output bounds and verifies image/AA budgets
// without allocating image or depth buffers.
[[nodiscard]] PhotoTextDimensionProjectionResult preflight_photo_text_dimension_mesh(
    const PhotoTextDimensionMesh& mesh,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderOptions& options = {});

struct PhotoTextDimensionRender {
  QImage image;
  // Canvas/raster coordinates represented by image pixel (0, 0). Bounds are
  // expanded for antialiasing and never fitted or silently clipped.
  QRect pixel_bounds;
  QPoint origin_offset;
  QString error;

  [[nodiscard]] bool is_valid() const { return error.isEmpty() && !image.isNull(); }
};

// CPU triangle rasterizer. Mesh triangles are depth-tested together; camera
// transforms rotate around pivot and triangles behind the near plane are
// rejected. The result keeps transparent antialiased edges and explicit
// expanded bounds for composition into the original canvas.
[[nodiscard]] PhotoTextDimensionRender render_photo_text_dimension_mesh(
    const PhotoTextDimensionMesh& mesh,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderStyle& style = {},
    const PhotoTextDimensionRenderOptions& options = {});

}  // namespace photo_editor::desktop
