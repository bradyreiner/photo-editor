#include "photo_text_dimension_mesh.h"

#include <QColorSpace>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <utility>

namespace photo_editor::desktop {
namespace {

constexpr double kGeometryEpsilon = 1.0e-10;
constexpr double kProjectionEpsilon = 1.0e-12;

struct Point2 {
  double x = 0.0;
  double y = 0.0;
};

struct Contour {
  std::vector<Point2> points;
  Point2 inward;
  int inward_orientation = 1;
  double bevel = 0.0;
  double lower_x = 0.0;
  double upper_x = 0.0;
  double lower_y = 0.0;
  double upper_y = 0.0;
};

struct Edge {
  Point2 a;
  Point2 b;
  std::size_t contour = 0;
  double min_x = 0.0;
  double max_x = 0.0;
  double min_y = 0.0;
  double max_y = 0.0;
};

struct Crossing {
  const Edge* edge = nullptr;
  double x = 0.0;
};

struct ProjectedVertex {
  double x = 0.0;
  double y = 0.0;
  double depth = 0.0;
  QVector3D normal;
};

struct ProjectedTriangle {
  std::array<ProjectedVertex, 3> vertices;
  PhotoTextDimensionFace face = PhotoTextDimensionFace::Front;
  QColor front_color;
  QColor color;
  PhotoTextDimensionMaterial material;
};

[[nodiscard]] double cross(Point2 a, Point2 b) {
  return a.x * b.y - a.y * b.x;
}

[[nodiscard]] Point2 add(Point2 a, Point2 b) {
  return {a.x + b.x, a.y + b.y};
}

[[nodiscard]] Point2 subtract(Point2 a, Point2 b) {
  return {a.x - b.x, a.y - b.y};
}

[[nodiscard]] Point2 multiply(Point2 a, double scalar) {
  return {a.x * scalar, a.y * scalar};
}

[[nodiscard]] double dot(Point2 a, Point2 b) {
  return a.x * b.x + a.y * b.y;
}

[[nodiscard]] double length(Point2 value) {
  return std::hypot(value.x, value.y);
}

[[nodiscard]] Point2 normalized(Point2 value) {
  const double magnitude = length(value);
  return magnitude > kGeometryEpsilon ? multiply(value, 1.0 / magnitude) : Point2{};
}

[[nodiscard]] bool finite(double value) {
  return std::isfinite(value);
}

[[nodiscard]] bool same_point(Point2 a, Point2 b, double epsilon = 1.0e-10) {
  return std::hypot(a.x - b.x, a.y - b.y) <= epsilon;
}

[[nodiscard]] double signed_area(const std::vector<Point2>& points) {
  double area = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const Point2 a = points[i];
    const Point2 b = points[(i + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area * 0.5;
}

[[nodiscard]] double point_segment_distance(Point2 point, Point2 a, Point2 b) {
  const Point2 edge = subtract(b, a);
  const double edge_length_squared = dot(edge, edge);
  if (edge_length_squared <= kGeometryEpsilon) return length(subtract(point, a));
  const double amount = std::clamp(dot(subtract(point, a), edge) / edge_length_squared, 0.0, 1.0);
  return length(subtract(point, add(a, multiply(edge, amount))));
}

[[nodiscard]] bool point_inside(const std::vector<Contour>& contours,
                                Qt::FillRule fill_rule,
                                Point2 point) {
  int winding = 0;
  bool odd = false;
  for (const auto& contour : contours) {
    if (point.x < contour.lower_x || point.x > contour.upper_x ||
        point.y < contour.lower_y || point.y > contour.upper_y) continue;
    for (std::size_t i = 0; i < contour.points.size(); ++i) {
      const Point2 a = contour.points[i];
      const Point2 b = contour.points[(i + 1) % contour.points.size()];
      const bool crosses = (a.y <= point.y && b.y > point.y) ||
                           (a.y > point.y && b.y <= point.y);
      if (!crosses) continue;
      const double x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
      if (x <= point.x) continue;
      odd = !odd;
      const double is_left = (b.x - a.x) * (point.y - a.y) -
                             (point.x - a.x) * (b.y - a.y);
      if (a.y <= point.y && b.y > point.y && is_left > 0.0) ++winding;
      else if (a.y > point.y && b.y <= point.y && is_left < 0.0) --winding;
    }
  }
  return fill_rule == Qt::OddEvenFill ? odd : winding != 0;
}

void flatten_cubic(Point2 p0, Point2 p1, Point2 p2, Point2 p3, double tolerance,
                   int depth, std::vector<Point2>& points) {
  const double flatness = std::max(point_segment_distance(p1, p0, p3),
                                   point_segment_distance(p2, p0, p3));
  if (depth >= 14 || flatness <= tolerance) {
    points.push_back(p3);
    return;
  }

  const Point2 p01 = multiply(add(p0, p1), 0.5);
  const Point2 p12 = multiply(add(p1, p2), 0.5);
  const Point2 p23 = multiply(add(p2, p3), 0.5);
  const Point2 p012 = multiply(add(p01, p12), 0.5);
  const Point2 p123 = multiply(add(p12, p23), 0.5);
  const Point2 middle = multiply(add(p012, p123), 0.5);
  flatten_cubic(p0, p01, p012, middle, tolerance, depth + 1, points);
  flatten_cubic(middle, p123, p23, p3, tolerance, depth + 1, points);
}

void finish_contour(std::vector<Point2>& points, std::vector<Contour>& contours) {
  while (points.size() > 1 && same_point(points.front(), points.back())) points.pop_back();
  std::vector<Point2> compact;
  compact.reserve(points.size());
  for (const auto point : points) {
    if (compact.empty() || !same_point(compact.back(), point)) compact.push_back(point);
  }
  while (compact.size() > 1 && same_point(compact.front(), compact.back())) compact.pop_back();
  if (compact.size() < 3 || std::abs(signed_area(compact)) < 1.0e-12) {
    points.clear();
    return;
  }
  Contour contour;
  contour.points = std::move(compact);
  contour.lower_x = contour.upper_x = contour.points.front().x;
  contour.lower_y = contour.upper_y = contour.points.front().y;
  for (const auto point : contour.points) {
    contour.lower_x = std::min(contour.lower_x, point.x);
    contour.upper_x = std::max(contour.upper_x, point.x);
    contour.lower_y = std::min(contour.lower_y, point.y);
    contour.upper_y = std::max(contour.upper_y, point.y);
  }
  contours.push_back(std::move(contour));
  points.clear();
}

[[nodiscard]] bool flatten_path(const QPainterPath& path,
                                double tolerance,
                                std::size_t max_elements,
                                std::size_t max_contours,
                                std::size_t max_points,
                                const std::function<bool()>& canceled,
                                std::vector<Contour>& contours,
                                QString& error) {
  const auto element_count = path.elementCount();
  if (element_count <= 0) {
    error = QStringLiteral("The text outline is empty.");
    return false;
  }
  if (static_cast<std::size_t>(element_count) > max_elements) {
    error = QStringLiteral("The text outline exceeds the path element budget.");
    return false;
  }

  std::vector<Point2> current_points;
  Point2 current{};
  Point2 contour_start{};
  bool has_current = false;
  std::size_t finished_points = 0;
  for (int i = 0; i < element_count; ++i) {
    if ((i & 127) == 0 && canceled && canceled()) {
      error = QStringLiteral("Text geometry generation canceled.");
      return false;
    }
    const auto element = path.elementAt(i);
    const Point2 point{element.x, element.y};
    if (!finite(point.x) || !finite(point.y) || std::abs(point.x) > 1.0e8 ||
        std::abs(point.y) > 1.0e8) {
      error = QStringLiteral("The text outline contains an invalid or unsupported coordinate.");
      return false;
    }
    if (element.type == QPainterPath::MoveToElement) {
      if (has_current) {
        finished_points += current_points.size();
        if (finished_points > max_points) {
          error = QStringLiteral("The flattened text outline exceeds the edge budget.");
          return false;
        }
        finish_contour(current_points, contours);
      }
      if (contours.size() > max_contours) {
        error = QStringLiteral("The text outline exceeds the contour budget.");
        return false;
      }
      current_points.push_back(point);
      current = contour_start = point;
      has_current = true;
    } else if (element.type == QPainterPath::LineToElement) {
      if (!has_current) {
        error = QStringLiteral("The text outline contains a line without a contour.");
        return false;
      }
      current_points.push_back(point);
      current = point;
    } else if (element.type == QPainterPath::CurveToElement) {
      if (!has_current || i + 2 >= element_count) {
        error = QStringLiteral("The text outline contains an incomplete curve.");
        return false;
      }
      const auto second = path.elementAt(i + 1);
      const auto third = path.elementAt(i + 2);
      const Point2 p2{second.x, second.y};
      const Point2 p3{third.x, third.y};
      if (!finite(p2.x) || !finite(p2.y) || !finite(p3.x) || !finite(p3.y) ||
          std::abs(p2.x) > 1.0e8 || std::abs(p2.y) > 1.0e8 ||
          std::abs(p3.x) > 1.0e8 || std::abs(p3.y) > 1.0e8) {
        error = QStringLiteral("The text outline contains an invalid curve coordinate.");
        return false;
      }
      flatten_cubic(current, point, p2, p3, tolerance, 0, current_points);
      current = p3;
      i += 2;
    }
    if (finished_points + current_points.size() > max_points) {
      error = QStringLiteral("The flattened text outline exceeds the edge budget.");
      return false;
    }
  }
  if (has_current) {
    if (!same_point(current, contour_start)) current_points.push_back(contour_start);
    finished_points += current_points.size();
    if (finished_points > max_points) {
      error = QStringLiteral("The flattened text outline exceeds the edge budget.");
      return false;
    }
    finish_contour(current_points, contours);
  }
  if (contours.empty()) {
    error = QStringLiteral("The text outline has no filled contours.");
    return false;
  }
  if (contours.size() > max_contours) {
    error = QStringLiteral("The text outline exceeds the contour budget.");
    return false;
  }
  return true;
}

[[nodiscard]] bool segments_properly_intersect(Point2 a, Point2 b, Point2 c, Point2 d,
                                               double* intersection_y = nullptr) {
  const Point2 r = subtract(b, a);
  const Point2 s = subtract(d, c);
  const double denominator = cross(r, s);
  if (std::abs(denominator) < 1.0e-14) return false;
  const Point2 delta = subtract(c, a);
  const double t = cross(delta, s) / denominator;
  const double u = cross(delta, r) / denominator;
  constexpr double endpoint_epsilon = 1.0e-9;
  if (t <= endpoint_epsilon || t >= 1.0 - endpoint_epsilon ||
      u <= endpoint_epsilon || u >= 1.0 - endpoint_epsilon) return false;
  if (intersection_y != nullptr) *intersection_y = a.y + r.y * t;
  return true;
}

[[nodiscard]] std::vector<Edge> make_edges(const std::vector<Contour>& contours) {
  std::vector<Edge> edges;
  for (std::size_t contour_index = 0; contour_index < contours.size(); ++contour_index) {
    const auto& contour = contours[contour_index];
    for (std::size_t i = 0; i < contour.points.size(); ++i) {
      const Point2 a = contour.points[i];
      const Point2 b = contour.points[(i + 1) % contour.points.size()];
      if (same_point(a, b)) continue;
      edges.push_back({a, b, contour_index,
                       std::min(a.x, b.x), std::max(a.x, b.x),
                       std::min(a.y, b.y), std::max(a.y, b.y)});
    }
  }
  return edges;
}

[[nodiscard]] double edge_x_at(const Edge& edge, double y) {
  const double dy = edge.b.y - edge.a.y;
  if (std::abs(dy) < 1.0e-16) return std::min(edge.a.x, edge.b.x);
  return edge.a.x + (y - edge.a.y) * (edge.b.x - edge.a.x) / dy;
}

[[nodiscard]] bool scanline_caps(const std::vector<Contour>& contours,
                                 Qt::FillRule fill_rule,
                                 double front_z,
                                 double back_z,
                                 bool include_back,
                                 const QColor& front_color,
                                 std::size_t max_triangles,
                                 const std::function<bool()>& canceled,
                                 std::vector<PhotoTextDimensionTriangle>& triangles,
                                 QString& error) {
  const auto edges = make_edges(contours);
  std::vector<double> y_values;
  y_values.reserve(edges.size() * 2U);
  for (const auto& edge : edges) {
    y_values.push_back(edge.a.y);
    y_values.push_back(edge.b.y);
  }

  // Split slabs where outline edges cross. Font contours normally do not
  // self-intersect, but including proper crossings keeps the scan ordering
  // valid for overlapping contours as well.
  std::vector<std::size_t> sorted_by_x(edges.size());
  std::iota(sorted_by_x.begin(), sorted_by_x.end(), std::size_t{0});
  std::sort(sorted_by_x.begin(), sorted_by_x.end(), [&](std::size_t lhs, std::size_t rhs) {
    return edges[lhs].min_x < edges[rhs].min_x;
  });
  std::size_t intersection_count = 0;
  std::size_t intersection_checks = 0;
  for (std::size_t ordered = 0; ordered < sorted_by_x.size(); ++ordered) {
    if (canceled && canceled()) {
      error = QStringLiteral("Text geometry generation canceled.");
      return false;
    }
    const auto& first = edges[sorted_by_x[ordered]];
    for (std::size_t next = ordered + 1; next < sorted_by_x.size(); ++next) {
      const auto& second = edges[sorted_by_x[next]];
      if (second.min_x > first.max_x) break;
      if (second.min_y >= first.max_y || second.max_y <= first.min_y) continue;
      if (++intersection_checks > 20000000U) {
        error = QStringLiteral("Text outline intersection analysis exceeds the work budget.");
        return false;
      }
      double y = 0.0;
      if (!segments_properly_intersect(first.a, first.b, second.a, second.b, &y)) continue;
      y_values.push_back(y);
      if (++intersection_count > 200000U) {
        error = QStringLiteral("The text outline has too many crossing edges.");
        return false;
      }
    }
  }
  std::sort(y_values.begin(), y_values.end());
  y_values.erase(std::unique(y_values.begin(), y_values.end(), [](double lhs, double rhs) {
    return std::abs(lhs - rhs) <= 1.0e-10 * std::max({1.0, std::abs(lhs), std::abs(rhs)});
  }), y_values.end());
  if (!edges.empty() && y_values.size() > 50000000U / edges.size()) {
    error = QStringLiteral("Text outline scan conversion exceeds the work budget.");
    return false;
  }

  auto emit_cap_triangle = [&](Point2 a, Point2 b, Point2 c, double z, QVector3D normal,
                               PhotoTextDimensionFace face, const QColor& color) {
    if (triangles.size() >= max_triangles) return false;
    PhotoTextDimensionTriangle triangle;
    triangle.vertices = {{{QVector3D(static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(z)), normal},
                          {QVector3D(static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(z)), normal},
                          {QVector3D(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(z)), normal}}};
    triangle.face = face;
    triangle.front_color = color;
    triangles.push_back(std::move(triangle));
    return true;
  };

  for (std::size_t slab = 0; slab + 1 < y_values.size(); ++slab) {
    if (canceled && canceled()) {
      error = QStringLiteral("Text geometry generation canceled.");
      return false;
    }
    const double y0 = y_values[slab];
    const double y1 = y_values[slab + 1];
    if (y1 - y0 <= 1.0e-12) continue;
    const double middle_y = (y0 + y1) * 0.5;
    std::vector<Crossing> crossings;
    for (const auto& edge : edges) {
      if (std::abs(edge.b.y - edge.a.y) <= 1.0e-16 ||
          middle_y <= edge.min_y || middle_y >= edge.max_y) continue;
      crossings.push_back({&edge, edge_x_at(edge, middle_y)});
    }
    std::sort(crossings.begin(), crossings.end(), [](const Crossing& lhs, const Crossing& rhs) {
      if (lhs.x != rhs.x) return lhs.x < rhs.x;
      return lhs.edge->contour < rhs.edge->contour;
    });
    int winding = 0;
    bool odd = false;
    for (std::size_t interval = 0; interval + 1 < crossings.size(); ++interval) {
      const auto& left = crossings[interval];
      const auto& right = crossings[interval + 1];
      odd = !odd;
      winding += left.edge->a.y > left.edge->b.y ? 1 : -1;
      if (right.x - left.x <= 1.0e-12) continue;
      const bool filled = fill_rule == Qt::OddEvenFill ? odd : winding != 0;
      if (!filled) continue;
      const Point2 p00{edge_x_at(*left.edge, y0), y0};
      const Point2 p01{edge_x_at(*left.edge, y1), y1};
      const Point2 p11{edge_x_at(*right.edge, y1), y1};
      const Point2 p10{edge_x_at(*right.edge, y0), y0};
      if (!emit_cap_triangle(p00, p11, p01, front_z, QVector3D(0, 0, 1), PhotoTextDimensionFace::Front, front_color) ||
          !emit_cap_triangle(p00, p10, p11, front_z, QVector3D(0, 0, 1), PhotoTextDimensionFace::Front, front_color)) {
        error = QStringLiteral("Text geometry exceeds the triangle budget.");
        return false;
      }
      if (include_back &&
          (!emit_cap_triangle(p00, p01, p11, back_z, QVector3D(0, 0, -1), PhotoTextDimensionFace::Back, {}) ||
           !emit_cap_triangle(p00, p11, p10, back_z, QVector3D(0, 0, -1), PhotoTextDimensionFace::Back, {}))) {
        error = QStringLiteral("Text geometry exceeds the triangle budget.");
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool contour_self_intersects(const std::vector<Point2>& points) {
  const auto count = points.size();
  std::vector<Edge> edges;
  edges.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const Point2 a = points[i];
    const Point2 b = points[(i + 1) % count];
    edges.push_back({a, b, 0, std::min(a.x, b.x), std::max(a.x, b.x),
                     std::min(a.y, b.y), std::max(a.y, b.y)});
  }
  std::vector<std::size_t> sorted(count);
  std::iota(sorted.begin(), sorted.end(), std::size_t{0});
  std::sort(sorted.begin(), sorted.end(), [&](std::size_t lhs, std::size_t rhs) {
    return edges[lhs].min_x < edges[rhs].min_x;
  });
  std::size_t candidate_checks = 0;
  for (std::size_t ordered = 0; ordered < count; ++ordered) {
    const auto& first = edges[sorted[ordered]];
    for (std::size_t next = ordered + 1; next < count; ++next) {
      const auto& second = edges[sorted[next]];
      if (second.min_x > first.max_x) break;
      if (++candidate_checks > 2000000U) return true;
      const std::size_t i = sorted[ordered];
      const std::size_t j = sorted[next];
      if (j == i || j == (i + 1) % count || i == (j + 1) % count) continue;
      double ignored = 0.0;
      if (second.min_y >= first.max_y || second.max_y <= first.min_y) continue;
      if (segments_properly_intersect(first.a, first.b, second.a, second.b, &ignored)) return true;
    }
  }
  return false;
}

[[nodiscard]] bool interior_normal(const std::vector<Contour>& contours,
                                   Qt::FillRule fill_rule,
                                   Contour& contour) {
  for (std::size_t i = 0; i < contour.points.size(); ++i) {
    const Point2 a = contour.points[i];
    const Point2 b = contour.points[(i + 1) % contour.points.size()];
    const Point2 direction = normalized(subtract(b, a));
    if (length(direction) <= kGeometryEpsilon) continue;
    const Point2 left{-direction.y, direction.x};
    const Point2 middle = multiply(add(a, b), 0.5);
    const double delta = std::max(1.0e-7, std::min(length(subtract(b, a)) * 1.0e-3, 1.0e-3));
    const bool left_inside = point_inside(contours, fill_rule, add(middle, multiply(left, delta)));
    const bool right_inside = point_inside(contours, fill_rule, subtract(middle, multiply(left, delta)));
    if (left_inside != right_inside) {
      contour.inward_orientation = left_inside ? 1 : -1;
      contour.inward = left_inside ? left : multiply(left, -1.0);
      return true;
    }
  }
  const double area = signed_area(contour.points);
  contour.inward_orientation = area >= 0.0 ? 1 : -1;
  contour.inward = {1.0, 0.0};
  // A positive-area contour's interior is on its left overall. The first
  // usable edge gives the proper inward normal in this degenerate fallback.
  for (std::size_t i = 0; i < contour.points.size(); ++i) {
    const Point2 direction = normalized(subtract(contour.points[(i + 1) % contour.points.size()], contour.points[i]));
    if (length(direction) <= kGeometryEpsilon) continue;
    contour.inward = area >= 0.0 ? Point2{-direction.y, direction.x} : Point2{direction.y, -direction.x};
    break;
  }
  return false;
}

[[nodiscard]] Point2 edge_inward_normal(Point2 direction, const Contour& contour) {
  return multiply(Point2{-direction.y, direction.x}, static_cast<double>(contour.inward_orientation));
}

[[nodiscard]] std::vector<Point2> inward_vertex_normals(const Contour& contour) {
  std::vector<Point2> result;
  const auto count = contour.points.size();
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const Point2 previous_direction = normalized(subtract(contour.points[i], contour.points[(i + count - 1) % count]));
    const Point2 next_direction = normalized(subtract(contour.points[(i + 1) % count], contour.points[i]));
    const Point2 previous = edge_inward_normal(previous_direction, contour);
    const Point2 next = edge_inward_normal(next_direction, contour);
    result.push_back(normalized(add(previous, next)));
  }
  return result;
}

[[nodiscard]] std::vector<Point2> offset_contour(const Contour& contour, double distance) {
  std::vector<Point2> result;
  const std::size_t count = contour.points.size();
  result.reserve(count);
  if (distance <= 0.0) return contour.points;
  for (std::size_t i = 0; i < count; ++i) {
    const Point2 previous = contour.points[(i + count - 1) % count];
    const Point2 current = contour.points[i];
    const Point2 next = contour.points[(i + 1) % count];
    const Point2 previous_direction = normalized(subtract(current, previous));
    const Point2 next_direction = normalized(subtract(next, current));
    if (length(previous_direction) <= kGeometryEpsilon || length(next_direction) <= kGeometryEpsilon) {
      result.push_back(current);
      continue;
    }
    const Point2 in_previous = edge_inward_normal(previous_direction, contour);
    const Point2 in_next = edge_inward_normal(next_direction, contour);
    const double denominator = 1.0 + std::clamp(dot(in_previous, in_next), -1.0, 1.0);
    Point2 displacement;
    if (denominator > 1.0e-5) {
      displacement = multiply(add(in_previous, in_next), distance / denominator);
    } else {
      displacement = multiply(contour.inward, distance * 4.0);
    }
    const double max_miter = distance * 4.0;
    const double miter_length = length(displacement);
    if (miter_length > max_miter) displacement = multiply(displacement, max_miter / miter_length);
    result.push_back(add(current, displacement));
  }
  return result;
}

[[nodiscard]] QVector3D to_qvector(Point2 point, double z) {
  return QVector3D(static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(z));
}

void set_mesh_bounds(PhotoTextDimensionMesh& mesh, bool& has_bounds) {
  for (const auto& triangle : mesh.triangles) {
    for (const auto& vertex : triangle.vertices) {
      const auto& p = vertex.position;
      if (!has_bounds) {
        mesh.lower = mesh.upper = p;
        has_bounds = true;
      } else {
        mesh.lower.setX(std::min(mesh.lower.x(), p.x()));
        mesh.lower.setY(std::min(mesh.lower.y(), p.y()));
        mesh.lower.setZ(std::min(mesh.lower.z(), p.z()));
        mesh.upper.setX(std::max(mesh.upper.x(), p.x()));
        mesh.upper.setY(std::max(mesh.upper.y(), p.y()));
        mesh.upper.setZ(std::max(mesh.upper.z(), p.z()));
      }
    }
  }
}

[[nodiscard]] bool emit_mesh_triangle(std::vector<PhotoTextDimensionTriangle>& triangles,
                                      std::size_t max_triangles,
                                      QVector3D p0, QVector3D p1, QVector3D p2,
                                      QVector3D n0, QVector3D n1, QVector3D n2,
                                      PhotoTextDimensionFace face,
                                      const QColor& front_color) {
  if (triangles.size() >= max_triangles) return false;
  PhotoTextDimensionTriangle triangle;
  triangle.vertices = {{{p0, n0}, {p1, n1}, {p2, n2}}};
  triangle.face = face;
  if (face == PhotoTextDimensionFace::Front) triangle.front_color = front_color;
  triangles.push_back(std::move(triangle));
  return true;
}

[[nodiscard]] bool emit_profile(std::vector<PhotoTextDimensionTriangle>& triangles,
                                std::size_t max_triangles,
                                const Contour& contour,
                                const std::vector<Point2>& inward,
                                double full_depth,
                                int segments,
                                bool front,
                                PhotoTextDimensionBevelProfile profile,
                                const QColor& front_color) {
  const double bevel = contour.bevel;
  if (bevel <= 0.0) return true;
  const double core_z = front ? -bevel : -full_depth + bevel;
  const auto count = contour.points.size();
  for (int step = 0; step < segments; ++step) {
    const double t0 = static_cast<double>(step) / segments;
    const double t1 = static_cast<double>(step + 1) / segments;
    const double theta0 = (std::numbers::pi * 0.5) * t0;
    const double theta1 = (std::numbers::pi * 0.5) * t1;
    const auto profile_values = [&](double t, double theta) {
      if (profile == PhotoTextDimensionBevelProfile::Chamfer) return std::array<double, 2>{bevel * t, bevel * t};
      return std::array<double, 2>{bevel * std::sin(theta), bevel * (1.0 - std::cos(theta))};
    };
    const auto values0 = profile_values(t0, theta0);
    const auto values1 = profile_values(t1, theta1);
    const double inset0 = values0[0];
    const double inset1 = values1[0];
    const double z0 = front ? core_z + values0[1] : core_z - values0[1];
    const double z1 = front ? core_z + values1[1] : core_z - values1[1];
    const double normal_theta0 = profile == PhotoTextDimensionBevelProfile::Chamfer
                                     ? std::numbers::pi * 0.25 : theta0;
    const double normal_theta1 = profile == PhotoTextDimensionBevelProfile::Chamfer
                                     ? std::numbers::pi * 0.25 : theta1;
    const double nz0 = (front ? 1.0 : -1.0) * std::sin(normal_theta0);
    const double nz1 = (front ? 1.0 : -1.0) * std::sin(normal_theta1);
    const auto ring0 = offset_contour(contour, inset0);
    const auto ring1 = offset_contour(contour, inset1);
    for (std::size_t edge = 0; edge < count; ++edge) {
      const std::size_t next = (edge + 1) % count;
      const QVector3D out_a(-static_cast<float>(inward[edge].x), -static_cast<float>(inward[edge].y), 0.0F);
      const QVector3D out_b(-static_cast<float>(inward[next].x), -static_cast<float>(inward[next].y), 0.0F);
      const QVector3D normal_a0 = (out_a * static_cast<float>(std::cos(normal_theta0)) + QVector3D(0, 0, static_cast<float>(nz0))).normalized();
      const QVector3D normal_b0 = (out_b * static_cast<float>(std::cos(normal_theta0)) + QVector3D(0, 0, static_cast<float>(nz0))).normalized();
      const QVector3D normal_a1 = (out_a * static_cast<float>(std::cos(normal_theta1)) + QVector3D(0, 0, static_cast<float>(nz1))).normalized();
      const QVector3D normal_b1 = (out_b * static_cast<float>(std::cos(normal_theta1)) + QVector3D(0, 0, static_cast<float>(nz1))).normalized();
      const QVector3D p00 = to_qvector(ring0[edge], z0);
      const QVector3D p10 = to_qvector(ring0[next], z0);
      const QVector3D p11 = to_qvector(ring1[next], z1);
      const QVector3D p01 = to_qvector(ring1[edge], z1);
      const bool use_forward_winding = front == (contour.inward_orientation > 0);
      if (use_forward_winding) {
        if (!emit_mesh_triangle(triangles, max_triangles, p00, p10, p11,
                                normal_a0, normal_b0, normal_b1,
                                PhotoTextDimensionFace::Bevel, front_color) ||
            !emit_mesh_triangle(triangles, max_triangles, p00, p11, p01,
                                normal_a0, normal_b1, normal_a1,
                                PhotoTextDimensionFace::Bevel, front_color)) return false;
      } else {
        if (!emit_mesh_triangle(triangles, max_triangles, p00, p11, p10,
                                normal_a0, normal_b1, normal_b0,
                                PhotoTextDimensionFace::Bevel, front_color) ||
            !emit_mesh_triangle(triangles, max_triangles, p00, p01, p11,
                                normal_a0, normal_a1, normal_b1,
                                PhotoTextDimensionFace::Bevel, front_color)) return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool emit_contour_walls(std::vector<PhotoTextDimensionTriangle>& triangles,
                                      std::size_t max_triangles,
                                      const Contour& contour,
                                      const std::vector<Point2>& inward,
                                      double full_depth,
                                      const QColor& front_color) {
  const double low_z = -full_depth + contour.bevel;
  const double high_z = -contour.bevel;
  const auto count = contour.points.size();
  for (std::size_t edge = 0; edge < count; ++edge) {
    const std::size_t next = (edge + 1) % count;
    const Point2 a = contour.points[edge];
    const Point2 b = contour.points[next];
    const QVector3D normal_a(-static_cast<float>(inward[edge].x), -static_cast<float>(inward[edge].y), 0.0F);
    const QVector3D normal_b(-static_cast<float>(inward[next].x), -static_cast<float>(inward[next].y), 0.0F);
    const QVector3D p00 = to_qvector(a, low_z);
    const QVector3D p10 = to_qvector(b, low_z);
    const QVector3D p11 = to_qvector(b, high_z);
    const QVector3D p01 = to_qvector(a, high_z);
    if (contour.inward_orientation > 0) {
      if (!emit_mesh_triangle(triangles, max_triangles, p00, p10, p11,
                              normal_a, normal_b, normal_b,
                              PhotoTextDimensionFace::Side, front_color) ||
          !emit_mesh_triangle(triangles, max_triangles, p00, p11, p01,
                              normal_a, normal_b, normal_a,
                              PhotoTextDimensionFace::Side, front_color)) return false;
    } else {
      if (!emit_mesh_triangle(triangles, max_triangles, p00, p11, p10,
                              normal_a, normal_b, normal_b,
                              PhotoTextDimensionFace::Side, front_color) ||
          !emit_mesh_triangle(triangles, max_triangles, p00, p01, p11,
                              normal_a, normal_a, normal_b,
                              PhotoTextDimensionFace::Side, front_color)) return false;
    }
  }
  return true;
}

[[nodiscard]] bool finite_vector(QVector3D vector) {
  return std::isfinite(vector.x()) && std::isfinite(vector.y()) && std::isfinite(vector.z());
}

[[nodiscard]] double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] double srgb_to_linear(double value) {
  return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] QColor material_color(const PhotoTextDimensionTriangle& triangle,
                                    const PhotoTextDimensionRenderStyle& style,
                                    PhotoTextDimensionMaterial& material) {
  switch (triangle.face) {
    case PhotoTextDimensionFace::Front:
      material = style.front;
      if (triangle.front_color.isValid()) material.color = triangle.front_color;
      break;
    case PhotoTextDimensionFace::Back: material = style.back; break;
    case PhotoTextDimensionFace::Side: material = style.side; break;
    case PhotoTextDimensionFace::Bevel: material = style.bevel; break;
  }
  return material.color;
}

[[nodiscard]] std::array<float, 4> shade_color(QColor color,
                                              const PhotoTextDimensionMaterial& material,
                                              QVector3D normal,
                                              const PhotoTextDimensionRenderStyle& style,
                                              QVector3D light_direction) {
  if (normal.lengthSquared() < 1.0e-12F) normal = QVector3D(0, 0, 1);
  else normal.normalize();
  const double ndotl = clamp_unit(QVector3D::dotProduct(normal, light_direction));
  QVector3D view(0, 0, 1);
  QVector3D half_vector = light_direction + view;
  if (half_vector.lengthSquared() > 1.0e-12F) half_vector.normalize();
  const double roughness = std::clamp(material.roughness, 0.04, 1.0);
  const double metallic = clamp_unit(material.metallic);
  const double exponent = std::clamp(2.0 / std::pow(roughness, 4.0) - 2.0, 2.0, 512.0);
  const double specular = style.specular_intensity *
      std::pow(clamp_unit(QVector3D::dotProduct(normal, half_vector)), exponent) * ndotl;
  const double diffuse = std::max(0.0, style.ambient + style.key_light * ndotl);
  const double alpha = color.alphaF();
  const double source[3] = {srgb_to_linear(color.redF()), srgb_to_linear(color.greenF()),
                            srgb_to_linear(color.blueF())};
  std::array<float, 4> output{};
  for (int channel = 0; channel < 3; ++channel) {
    const double f0 = (1.0 - metallic) * 0.04 + metallic * source[channel];
    const double linear = source[channel] * diffuse * (1.0 - metallic * 0.25) + f0 * specular;
    output[static_cast<std::size_t>(channel)] = static_cast<float>(std::max(0.0, linear));
  }
  output[3] = static_cast<float>(alpha);
  return output;
}

[[nodiscard]] bool project_vertex(const PhotoTextDimensionVertex& vertex,
                                  const PhotoTextDimensionCamera& camera,
                                  const PhotoTextDimensionRenderOptions& options,
                                  ProjectedVertex& result) {
  if (camera.projection != PhotoTextDimensionProjection::Orthographic &&
      camera.projection != PhotoTextDimensionProjection::Perspective) return false;
  if (!finite(options.output_scale.x()) || !finite(options.output_scale.y()) ||
      options.output_scale.x() <= 0.0 || options.output_scale.y() <= 0.0 ||
      !finite(camera.screen_origin.x()) || !finite(camera.screen_origin.y()) ||
      !finite(camera.camera_distance) || !finite(camera.near_plane) || camera.near_plane <= 0.0) return false;
  const double quaternion_length = camera.rotation.lengthSquared();
  if (!finite(camera.rotation.scalar()) || !finite(camera.rotation.x()) ||
      !finite(camera.rotation.y()) || !finite(camera.rotation.z()) ||
      !finite(quaternion_length) || quaternion_length < 1.0e-12) return false;
  const QQuaternion rotation = camera.rotation.normalized();
  const QVector3D local = vertex.position - camera.pivot;
  QVector3D rotated = rotation.rotatedVector(local);
  if (!finite_vector(rotated)) return false;
  result.normal = rotation.rotatedVector(vertex.normal);
  if (!finite_vector(result.normal)) return false;
  if (result.normal.lengthSquared() > 1.0e-12F) result.normal.normalize();
  result.depth = camera.camera_distance - static_cast<double>(rotated.z());
  if (!finite(result.depth) || result.depth <= camera.near_plane) return false;
  if (camera.projection == PhotoTextDimensionProjection::Orthographic) {
    result.x = camera.screen_origin.x() + rotated.x() * camera.orthographic_pixels_per_unit;
    result.y = camera.screen_origin.y() + rotated.y() * camera.orthographic_pixels_per_unit;
  } else {
    const double factor = camera.perspective_focal_length_pixels / result.depth;
    result.x = camera.screen_origin.x() + rotated.x() * factor;
    result.y = camera.screen_origin.y() + rotated.y() * factor;
  }
  result.x *= options.output_scale.x();
  result.y *= options.output_scale.y();
  return finite(result.x) && finite(result.y);
}

[[nodiscard]] double edge_function(double ax, double ay, double bx, double by,
                                   double px, double py) {
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

[[nodiscard]] bool valid_material(const PhotoTextDimensionMaterial& material) {
  return material.color.isValid() && finite(material.roughness) && finite(material.metallic) &&
         material.roughness >= 0.0 && material.roughness <= 1.0 &&
         material.metallic >= 0.0 && material.metallic <= 1.0;
}

}  // namespace

PhotoTextDimensionMesh build_photo_text_dimension_mesh(
    const QPainterPath& path,
    const PhotoTextDimensionGeometryOptions& options) {
  PhotoTextDimensionMesh mesh;
  const auto fail = [&](QString message) {
    mesh.triangles.clear();
    mesh.error = std::move(message);
    return mesh;
  };
  if (!finite(options.depth) || options.depth < 0.0 || options.depth > 100000.0 ||
      !finite(options.bevel_width) || options.bevel_width < 0.0 || options.bevel_width > 50000.0 ||
      (options.bevel_profile != PhotoTextDimensionBevelProfile::Rounded &&
       options.bevel_profile != PhotoTextDimensionBevelProfile::Chamfer) ||
      options.bevel_segments < 1 || options.bevel_segments > 32 ||
      !finite(options.flatten_tolerance) || options.flatten_tolerance < 0.01 ||
      options.flatten_tolerance > 8.0 || options.max_path_elements == 0 ||
      options.max_contours == 0 || options.max_edges < 3 || options.max_triangles < 4) {
    return fail(QStringLiteral("Text geometry settings are outside the supported range."));
  }
  if (options.canceled && options.canceled()) return fail(QStringLiteral("Text geometry generation canceled."));

  std::vector<Contour> contours;
  if (!flatten_path(path, options.flatten_tolerance, options.max_path_elements,
                    options.max_contours, options.max_edges, options.canceled, contours, mesh.error)) return mesh;
  const auto edges = make_edges(contours);
  if (edges.size() > options.max_edges) return fail(QStringLiteral("Text geometry exceeds the edge budget."));
  for (auto& contour : contours) (void)interior_normal(contours, path.fillRule(), contour);

  for (auto& contour : contours) {
    const double local_extent = std::min(contour.upper_x - contour.lower_x,
                                         contour.upper_y - contour.lower_y);
    // A contour cannot support a bevel wider than a small fraction of its
    // narrow dimension without collapsing counters or narrow strokes.
    contour.bevel = std::min({options.bevel_width, options.depth * 0.5,
                              std::max(0.0, local_extent * 0.18)});
    bool safe_offset = contour.bevel <= 0.0;
    for (int attempt = 0; attempt < 10 && !safe_offset; ++attempt) {
      const auto candidate = offset_contour(contour, contour.bevel);
      const double candidate_area = signed_area(candidate);
      const double original_area = signed_area(contour.points);
      double maximum_motion = 0.0;
      for (std::size_t point = 0; point < candidate.size(); ++point)
        maximum_motion = std::max(maximum_motion, length(subtract(candidate[point], contour.points[point])));
      safe_offset = candidate.size() == contour.points.size() &&
                    candidate_area * original_area > 0.0 &&
                    std::abs(candidate_area) > std::abs(original_area) * 1.0e-6 &&
                    maximum_motion > 1.0e-9 &&
                    !contour_self_intersects(candidate);
      if (!safe_offset) contour.bevel *= 0.5;
    }
    if (!safe_offset) contour.bevel = 0.0;
  }

  std::vector<Contour> inset_contours = contours;
  for (std::size_t i = 0; i < contours.size(); ++i) {
    inset_contours[i].points = offset_contour(contours[i], contours[i].bevel);
  }
  const double front_cap_z = 0.0;
  const double back_cap_z = -options.depth;
  if (!scanline_caps(inset_contours, path.fillRule(), front_cap_z, back_cap_z, options.depth > 0.0,
                     options.front_color, options.max_triangles, options.canceled,
                     mesh.triangles, mesh.error)) return mesh;

  for (const auto& contour : contours) {
    if (options.canceled && options.canceled()) return fail(QStringLiteral("Text geometry generation canceled."));
    const auto inward_normals = inward_vertex_normals(contour);
    if (!emit_contour_walls(mesh.triangles, options.max_triangles, contour, inward_normals,
                            options.depth, options.front_color)) {
      return fail(QStringLiteral("Text geometry exceeds the triangle budget."));
    }
    if (!emit_profile(mesh.triangles, options.max_triangles, contour, inward_normals,
                      options.depth, options.bevel_segments, true, options.bevel_profile, options.front_color) ||
        !emit_profile(mesh.triangles, options.max_triangles, contour, inward_normals,
                      options.depth, options.bevel_segments, false, options.bevel_profile, options.front_color)) {
      return fail(QStringLiteral("Text geometry exceeds the triangle budget."));
    }
  }
  if (mesh.triangles.empty()) return fail(QStringLiteral("The text outline produced no triangles."));
  bool has_bounds = false;
  set_mesh_bounds(mesh, has_bounds);
  if (!has_bounds) return fail(QStringLiteral("The text outline produced no usable geometry."));
  return mesh;
}

QPointF project_photo_text_dimension_point(
    QVector3D point,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderOptions& options,
    bool* valid) {
  PhotoTextDimensionVertex vertex{point, QVector3D(0, 0, 1)};
  ProjectedVertex projected;
  const bool is_valid = project_vertex(vertex, camera, options, projected);
  if (valid != nullptr) *valid = is_valid;
  return is_valid ? QPointF(projected.x, projected.y) : QPointF();
}

PhotoTextDimensionProjectionResult preflight_photo_text_dimension_mesh(
    const PhotoTextDimensionMesh& mesh,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderOptions& options) {
  PhotoTextDimensionProjectionResult result;
  const auto fail = [&](QString message) {
    result.error = std::move(message);
    result.pixel_bounds = {};
    result.origin_offset = {};
    return result;
  };
  if (!mesh.error.isEmpty()) return fail(mesh.error);
  if (mesh.triangles.empty()) return fail(QStringLiteral("There is no 3D text geometry to project."));
  if (!finite(options.output_scale.x()) || !finite(options.output_scale.y()) ||
      options.output_scale.x() <= 0.0 || options.output_scale.y() <= 0.0 ||
      options.antialias_samples < 1 || options.antialias_samples > 4 ||
      options.max_dimension <= 0 || options.max_triangles == 0 || options.max_output_pixels == 0 ||
      options.max_working_samples == 0 || options.max_working_bytes == 0 ||
      !options.reference_rect.isValid() ||
      !finite(options.reference_rect.x()) || !finite(options.reference_rect.y()) ||
      !finite(options.reference_rect.width()) || !finite(options.reference_rect.height()) ||
      !finite(camera.screen_origin.x()) || !finite(camera.screen_origin.y()) ||
      !finite(camera.camera_distance) || !finite(camera.near_plane) || camera.near_plane <= 0.0 ||
      camera.camera_distance <= camera.near_plane ||
      !finite(camera.orthographic_pixels_per_unit) || camera.orthographic_pixels_per_unit <= 0.0 ||
      !finite(camera.perspective_focal_length_pixels) || camera.perspective_focal_length_pixels <= 0.0 ||
      !finite(camera.pivot.x()) || !finite(camera.pivot.y()) || !finite(camera.pivot.z()) ||
      !finite_vector(camera.rotation.rotatedVector(QVector3D(0, 0, 1)))) {
    return fail(QStringLiteral("3D text camera or render settings are outside the supported range."));
  }
  if (camera.projection != PhotoTextDimensionProjection::Orthographic &&
      camera.projection != PhotoTextDimensionProjection::Perspective) {
    return fail(QStringLiteral("3D text projection mode is unsupported."));
  }
  if (mesh.triangles.size() > options.max_triangles) {
    return fail(QStringLiteral("3D text geometry exceeds the triangle budget."));
  }
  if (options.canceled && options.canceled()) return fail(QStringLiteral("3D text projection canceled."));

  double geometry_left = std::numeric_limits<double>::infinity();
  double geometry_top = std::numeric_limits<double>::infinity();
  double geometry_right = -std::numeric_limits<double>::infinity();
  double geometry_bottom = -std::numeric_limits<double>::infinity();
  bool has_projected_geometry = false;
  for (const auto& triangle : mesh.triangles) {
    if (options.canceled && options.canceled()) return fail(QStringLiteral("3D text projection canceled."));
    std::array<ProjectedVertex, 3> vertices;
    for (std::size_t i = 0; i < 3; ++i) {
      if (!finite_vector(triangle.vertices[i].position) || !finite_vector(triangle.vertices[i].normal) ||
          !project_vertex(triangle.vertices[i], camera, options, vertices[i])) {
        return fail(QStringLiteral(
            "Part of the 3D text crosses the camera near plane. Reduce extrusion or tilt, or increase the camera distance."));
      }
    }
    for (const auto& vertex : vertices) {
      geometry_left = std::min(geometry_left, vertex.x);
      geometry_top = std::min(geometry_top, vertex.y);
      geometry_right = std::max(geometry_right, vertex.x);
      geometry_bottom = std::max(geometry_bottom, vertex.y);
    }
    has_projected_geometry = true;
  }
  if (!has_projected_geometry) return fail(QStringLiteral("All 3D text triangles were behind the camera near plane."));

  const double reference_left = options.reference_rect.left() * options.output_scale.x();
  const double reference_top = options.reference_rect.top() * options.output_scale.y();
  const double reference_right = options.reference_rect.right() * options.output_scale.x();
  const double reference_bottom = options.reference_rect.bottom() * options.output_scale.y();
  if (!finite(reference_left) || !finite(reference_top) || !finite(reference_right) || !finite(reference_bottom) ||
      std::abs(geometry_left) > 1.0e9 || std::abs(geometry_top) > 1.0e9 ||
      std::abs(geometry_right) > 1.0e9 || std::abs(geometry_bottom) > 1.0e9 ||
      std::abs(reference_left) > 1.0e9 || std::abs(reference_top) > 1.0e9 ||
      std::abs(reference_right) > 1.0e9 || std::abs(reference_bottom) > 1.0e9) {
    return fail(QStringLiteral("The projected text bounds exceed supported raster coordinates."));
  }
  constexpr qint64 antialias_margin = 1;
  qint64 left = 0;
  qint64 top = 0;
  qint64 right = -1;
  qint64 bottom = -1;
  if (options.symmetric_padding) {
    const qint64 authored_left = static_cast<qint64>(std::floor(reference_left));
    const qint64 authored_top = static_cast<qint64>(std::floor(reference_top));
    const qint64 authored_right = static_cast<qint64>(std::ceil(reference_right));
    const qint64 authored_bottom = static_cast<qint64>(std::ceil(reference_bottom));
    const qint64 padding_x = std::max(antialias_margin, static_cast<qint64>(std::ceil(std::max(
        static_cast<double>(authored_left) - geometry_left,
        geometry_right - static_cast<double>(authored_right)))) + antialias_margin);
    const qint64 padding_y = std::max(antialias_margin, static_cast<qint64>(std::ceil(std::max(
        static_cast<double>(authored_top) - geometry_top,
        geometry_bottom - static_cast<double>(authored_bottom)))) + antialias_margin);
    left = authored_left - padding_x;
    top = authored_top - padding_y;
    right = authored_right + padding_x;
    bottom = authored_bottom + padding_y;
  } else {
    left = static_cast<int>(std::floor(std::min(geometry_left, reference_left))) - antialias_margin;
    top = static_cast<int>(std::floor(std::min(geometry_top, reference_top))) - antialias_margin;
    right = static_cast<int>(std::ceil(std::max(geometry_right, reference_right))) + antialias_margin;
    bottom = static_cast<int>(std::ceil(std::max(geometry_bottom, reference_bottom))) + antialias_margin;
  }
  const qint64 width = right - left;
  const qint64 height = bottom - top;
  if (width <= 0 || height <= 0 || width > options.max_dimension || height > options.max_dimension ||
      left < std::numeric_limits<int>::min() || left > std::numeric_limits<int>::max() ||
      top < std::numeric_limits<int>::min() || top > std::numeric_limits<int>::max()) {
    return fail(QStringLiteral("Expanded 3D text bounds exceed the image dimension budget."));
  }
  const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  const std::uint64_t samples_per_axis = static_cast<std::uint64_t>(options.antialias_samples);
  if (width > std::numeric_limits<int>::max() / static_cast<qint64>(samples_per_axis) ||
      height > std::numeric_limits<int>::max() / static_cast<qint64>(samples_per_axis)) {
    return fail(QStringLiteral("Expanded 3D text bounds exceed the sample dimension budget."));
  }
  if (pixel_count > std::numeric_limits<std::uint64_t>::max() /
                        (samples_per_axis * samples_per_axis)) {
    return fail(QStringLiteral("Expanded 3D text bounds exceed the sample budget."));
  }
  const std::uint64_t sample_count = pixel_count * samples_per_axis * samples_per_axis;
  constexpr std::uint64_t bytes_per_sample = sizeof(float) * 5U;  // linear RGBA + depth.
  constexpr std::uint64_t bytes_per_output_pixel = sizeof(float) * 4U;
  constexpr std::uint64_t bytes_per_projected_triangle = sizeof(ProjectedTriangle);
  const bool byte_count_overflow =
      sample_count > std::numeric_limits<std::uint64_t>::max() / bytes_per_sample ||
      pixel_count > std::numeric_limits<std::uint64_t>::max() / bytes_per_output_pixel ||
      mesh.triangles.size() > std::numeric_limits<std::uint64_t>::max() /
                                  bytes_per_projected_triangle;
  const std::uint64_t sample_bytes = byte_count_overflow ? 0 : sample_count * bytes_per_sample;
  const std::uint64_t output_bytes = byte_count_overflow ? 0 : pixel_count * bytes_per_output_pixel;
  const std::uint64_t projected_bytes = byte_count_overflow ? 0 :
      static_cast<std::uint64_t>(mesh.triangles.size()) * bytes_per_projected_triangle;
  if (pixel_count > options.max_output_pixels || sample_count > options.max_working_samples ||
      byte_count_overflow || sample_bytes > options.max_working_bytes ||
      output_bytes > options.max_working_bytes - sample_bytes ||
      projected_bytes > options.max_working_bytes - sample_bytes - output_bytes) {
    return fail(QStringLiteral("Expanded 3D text bounds exceed the pixel or memory budget."));
  }
  result.pixel_bounds = QRect(static_cast<int>(left), static_cast<int>(top),
                              static_cast<int>(width), static_cast<int>(height));
  result.origin_offset = result.pixel_bounds.topLeft();
  return result;
}

PhotoTextDimensionRender render_photo_text_dimension_mesh(
    const PhotoTextDimensionMesh& mesh,
    const PhotoTextDimensionCamera& camera,
    const PhotoTextDimensionRenderStyle& style,
    const PhotoTextDimensionRenderOptions& options) {
  PhotoTextDimensionRender result;
  const auto fail = [&](QString message) {
    result.image = {};
    result.error = std::move(message);
    return result;
  };
  if (options.canceled && options.canceled()) return fail(QStringLiteral("3D text rendering canceled."));
  const auto preflight = preflight_photo_text_dimension_mesh(mesh, camera, options);
  if (!preflight.is_valid()) return fail(preflight.error);
  if (!valid_material(style.front) || !valid_material(style.back) ||
      !valid_material(style.side) || !valid_material(style.bevel) ||
      !finite(style.ambient) || !finite(style.key_light) || !finite(style.specular_intensity) ||
      style.ambient < 0.0 || style.ambient > 2.0 || style.key_light < 0.0 ||
      style.key_light > 64.0 || style.specular_intensity < 0.0 || style.specular_intensity > 2.0) {
    return fail(QStringLiteral("3D text material settings are outside the supported range."));
  }
  QVector3D light = style.light_direction;
  if (!finite_vector(light) || light.lengthSquared() < 1.0e-10F) {
    return fail(QStringLiteral("3D text light direction must be finite and nonzero."));
  }
  light.normalize();

  std::vector<ProjectedTriangle> projected;
  projected.reserve(mesh.triangles.size());
  for (const auto& triangle : mesh.triangles) {
    if (options.canceled && options.canceled()) return fail(QStringLiteral("3D text rendering canceled."));
    ProjectedTriangle output;
    output.face = triangle.face;
    output.front_color = triangle.front_color;
    output.color = material_color(triangle, style, output.material);
    for (std::size_t i = 0; i < 3; ++i) {
      if (!project_vertex(triangle.vertices[i], camera, options, output.vertices[i])) {
        return fail(QStringLiteral(
            "Part of the 3D text crosses the camera near plane. Reduce extrusion or tilt, or increase the camera distance."));
      }
    }
    projected.push_back(std::move(output));
  }
  if (projected.empty()) return fail(QStringLiteral("There is no visible 3D text geometry to render."));

  const QRect bounds = preflight.pixel_bounds;
  const int left = bounds.x();
  const int top = bounds.y();
  const int width = bounds.width();
  const int height = bounds.height();
  const int samples = options.antialias_samples;
  const int sample_width = width * samples;
  const int sample_height = height * samples;
  const std::size_t sample_count = static_cast<std::size_t>(sample_width) * static_cast<std::size_t>(sample_height);
  QImage high_res(sample_width, sample_height, QImage::Format_RGBA32FPx4);
  if (high_res.isNull()) return fail(QStringLiteral("Cannot allocate the 3D text render buffer."));
  high_res.fill(Qt::transparent);
  std::vector<float> depth_buffer(sample_count, std::numeric_limits<float>::infinity());

  for (const auto& triangle : projected) {
    if (options.canceled && options.canceled()) return fail(QStringLiteral("3D text rendering canceled."));
    const auto& a = triangle.vertices[0];
    const auto& b = triangle.vertices[1];
    const auto& c = triangle.vertices[2];
    const double ax = (a.x - left) * samples;
    const double ay = (a.y - top) * samples;
    const double bx = (b.x - left) * samples;
    const double by = (b.y - top) * samples;
    const double cx = (c.x - left) * samples;
    const double cy = (c.y - top) * samples;
    const double area = edge_function(ax, ay, bx, by, cx, cy);
    if (!finite(area) || std::abs(area) <= 1.0e-12) continue;
    const int min_x = std::clamp(static_cast<int>(std::floor(std::min({ax, bx, cx}))), 0, sample_width - 1);
    const int max_x = std::clamp(static_cast<int>(std::ceil(std::max({ax, bx, cx}))), 0, sample_width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor(std::min({ay, by, cy}))), 0, sample_height - 1);
    const int max_y = std::clamp(static_cast<int>(std::ceil(std::max({ay, by, cy}))), 0, sample_height - 1);
    if (min_x > max_x || min_y > max_y) continue;

    for (int y = min_y; y <= max_y; ++y) {
      if (options.canceled && (y & 63) == 0 && options.canceled()) return fail(QStringLiteral("3D text rendering canceled."));
      auto* scanline = reinterpret_cast<float*>(high_res.scanLine(y));
      for (int x = min_x; x <= max_x; ++x) {
        const double px = x + 0.5;
        const double py = y + 0.5;
        const double wa = edge_function(bx, by, cx, cy, px, py) / area;
        const double wb = edge_function(cx, cy, ax, ay, px, py) / area;
        const double wc = 1.0 - wa - wb;
        if (wa < -1.0e-9 || wb < -1.0e-9 || wc < -1.0e-9) continue;
        const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(sample_width) + static_cast<std::size_t>(x);
        double depth = 0.0;
        QVector3D normal;
        if (camera.projection == PhotoTextDimensionProjection::Orthographic) {
          depth = wa * a.depth + wb * b.depth + wc * c.depth;
          normal = a.normal * static_cast<float>(wa) + b.normal * static_cast<float>(wb) + c.normal * static_cast<float>(wc);
        } else {
          const double qa = wa / a.depth;
          const double qb = wb / b.depth;
          const double qc = wc / c.depth;
          const double inverse_depth = qa + qb + qc;
          if (inverse_depth <= kProjectionEpsilon) continue;
          depth = 1.0 / inverse_depth;
          normal = (a.normal * static_cast<float>(qa) + b.normal * static_cast<float>(qb) + c.normal * static_cast<float>(qc)) /
                   static_cast<float>(inverse_depth);
        }
        if (depth >= depth_buffer[offset]) continue;
        depth_buffer[offset] = static_cast<float>(depth);
        const auto shaded = shade_color(triangle.color, triangle.material, normal, style, light);
        const std::size_t rgba_offset = static_cast<std::size_t>(x) * 4U;
        scanline[rgba_offset] = shaded[0];
        scanline[rgba_offset + 1U] = shaded[1];
        scanline[rgba_offset + 2U] = shaded[2];
        scanline[rgba_offset + 3U] = shaded[3];
      }
    }
  }

  QImage output(width, height, QImage::Format_RGBA32FPx4);
  if (output.isNull()) return fail(QStringLiteral("Cannot allocate the final 3D text image."));
  output.fill(Qt::transparent);
  const double sample_total = static_cast<double>(samples * samples);
  for (int y = 0; y < height; ++y) {
    auto* destination = reinterpret_cast<float*>(output.scanLine(y));
    for (int x = 0; x < width; ++x) {
      double red = 0.0;
      double green = 0.0;
      double blue = 0.0;
      double alpha_sum = 0.0;
      for (int sy = 0; sy < samples; ++sy) {
        const auto* source = reinterpret_cast<const float*>(high_res.constScanLine(y * samples + sy));
        for (int sx = 0; sx < samples; ++sx) {
          const std::size_t source_offset = static_cast<std::size_t>(x * samples + sx) * 4U;
          const double alpha = source[source_offset + 3U];
          red += source[source_offset] * alpha;
          green += source[source_offset + 1U] * alpha;
          blue += source[source_offset + 2U] * alpha;
          alpha_sum += alpha;
        }
      }
      const std::size_t destination_offset = static_cast<std::size_t>(x) * 4U;
      const double output_alpha = alpha_sum / sample_total;
      if (alpha_sum > 1.0e-12) {
        destination[destination_offset] = static_cast<float>(red / alpha_sum);
        destination[destination_offset + 1U] = static_cast<float>(green / alpha_sum);
        destination[destination_offset + 2U] = static_cast<float>(blue / alpha_sum);
      } else {
        destination[destination_offset] = 0.0F;
        destination[destination_offset + 1U] = 0.0F;
        destination[destination_offset + 2U] = 0.0F;
      }
      destination[destination_offset + 3U] = static_cast<float>(output_alpha);
    }
  }
  output.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  result.image = std::move(output);
  result.pixel_bounds = bounds;
  result.origin_offset = preflight.origin_offset;
  return result;
}

}  // namespace photo_editor::desktop
