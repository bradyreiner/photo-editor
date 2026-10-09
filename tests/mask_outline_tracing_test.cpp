#include "mask_outline_tracing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double actual, double expected) {
  return std::abs(actual - expected) < 1e-12;
}

void fill_rect(
    QByteArray* data,
    int width,
    int left,
    int top,
    int right,
    int bottom,
    unsigned char alpha = 255) {
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      (*data)[y * width + x] = static_cast<char>(alpha);
    }
  }
}

QVariantMap segmentation_mask(const QByteArray& data, int width, int height) {
  QVariantMap mask;
  mask.insert(QStringLiteral("segmentationData"), data);
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

bool normalized_path(const QVariantList& path) {
  if (path.size() < 4) {
    return false;
  }
  for (const auto& value : path) {
    const auto point = value.toMap();
    const auto x = point.value(QStringLiteral("x")).toDouble();
    const auto y = point.value(QStringLiteral("y")).toDouble();
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const QByteArray truncated(8, '\0');
  const auto invalid_mask = segmentation_mask(truncated, 4, 4);
  if (!traced_outline_paths_for_alpha_mask(truncated, 4, 4).isEmpty() ||
      !traced_outline_paths_for_alpha_mask_with_edge_padding(truncated, 4, 4).isEmpty() ||
      !hull_points_for_segmentation_mask(invalid_mask).isEmpty() ||
      !bounding_box_points_for_segmentation_mask(invalid_mask).isEmpty()) {
    return fail("outline tracing and fallbacks must reject truncated rasters");
  }

  QByteArray rectangular(25, '\0');
  fill_rect(&rectangular, 5, 1, 2, 3, 4);
  const auto mask = segmentation_mask(rectangular, 5, 5);
  const auto bounds = bounding_box_points_for_segmentation_mask(mask);
  if (bounds.size() != 4 ||
      !close(bounds.at(0).toMap().value(QStringLiteral("x")).toDouble(), 0.25) ||
      !close(bounds.at(0).toMap().value(QStringLiteral("y")).toDouble(), 0.5) ||
      !close(bounds.at(2).toMap().value(QStringLiteral("x")).toDouble(), 0.75) ||
      !close(bounds.at(2).toMap().value(QStringLiteral("y")).toDouble(), 1.0)) {
    return fail("segmentation bounds must publish exact normalized corners");
  }
  const auto hull = hull_points_for_segmentation_mask(mask);
  if (hull.size() < 3 || !normalized_path(hull)) {
    return fail("segmentation hull fallback must publish normalized boundary geometry");
  }

  QByteArray low_alpha(49, '\0');
  low_alpha[3 * 7 + 3] = static_cast<char>(20);
  const auto low_paths = traced_outline_paths_for_alpha_mask(low_alpha, 7, 7);
  if (low_paths.size() != 1 || !normalized_path(low_paths.first().toList())) {
    return fail("dynamic outline admission must retain a valid low-alpha subject");
  }

  QByteArray components(30 * 20, '\0');
  fill_rect(&components, 30, 1, 1, 8, 8);
  fill_rect(&components, 30, 15, 2, 18, 5);
  fill_rect(&components, 30, 28, 18, 28, 18);
  const auto component_paths = traced_outline_paths_for_alpha_mask(components, 30, 20);
  if (component_paths.size() != 3 ||
      component_paths != traced_outline_paths_for_alpha_mask(components, 30, 20)) {
    return fail("outline tracing must retain real islands and remain deterministic");
  }

  QByteArray edge_filled(12, static_cast<char>(255));
  const auto edge_paths =
      traced_outline_paths_for_alpha_mask_with_edge_padding(edge_filled, 4, 3);
  if (edge_paths.size() != 1 || !normalized_path(edge_paths.first().toList())) {
    return fail("edge-touching alpha masks must retain a bounded closed outline");
  }

  // A feathered diagonal is a linear alpha field with an analytic midpoint.
  // Contours must interpolate that field, not follow its thresholded pixel cells.
  constexpr int diagonal_width = 96;
  constexpr int diagonal_height = 80;
  QByteArray diagonal(diagonal_width * diagonal_height, '\0');
  for (int y = 0; y < diagonal_height; ++y) {
    for (int x = 0; x < diagonal_width; ++x) {
      const auto alpha = std::clamp(127.5 + 24.0 * (32.0 - x - 0.37 * y), 0.0, 255.0);
      diagonal[y * diagonal_width + x] = static_cast<char>(std::lround(alpha));
    }
  }
  const auto diagonal_paths = traced_outline_paths_for_alpha_mask(diagonal, diagonal_width, diagonal_height);
  double diagonal_error = 0;
  int diagonal_samples = 0;
  double diagonal_first_y = diagonal_height;
  double diagonal_last_y = 0;
  for (const auto& value : diagonal_paths) {
    for (const auto& point_value : value.toList()) {
      const auto point = point_value.toMap();
      const auto x = point.value(QStringLiteral("x")).toDouble() * (diagonal_width - 1);
      const auto y = point.value(QStringLiteral("y")).toDouble() * (diagonal_height - 1);
      if (x <= 1 || x >= diagonal_width - 2 || y <= 1 || y >= diagonal_height - 2) continue;
      diagonal_error = std::max(diagonal_error, std::abs(x + 0.37 * y - 32.0) / std::hypot(1.0, 0.37));
      diagonal_first_y = std::min(diagonal_first_y, y);
      diagonal_last_y = std::max(diagonal_last_y, y);
      ++diagonal_samples;
    }
  }
  if (diagonal_samples < 2 || diagonal_last_y - diagonal_first_y < 60 || diagonal_error > 0.06)
    return fail("feathered diagonal contours must follow canonical alpha nodes within subpixel error");

  constexpr int ring_edge = 128;
  QByteArray ring(ring_edge * ring_edge, '\0');
  for (int y = 0; y < ring_edge; ++y) {
    for (int x = 0; x < ring_edge; ++x) {
      const double radius = std::hypot(x - 58.4, y - 60.7);
      const double coverage = std::min(radius - 3.7, 30.3 - radius);
      ring[y * ring_edge + x] = static_cast<char>(std::lround(std::clamp(127.5 + 48.0 * coverage, 0.0, 255.0)));
    }
  }
  const auto ring_paths = traced_outline_paths_for_alpha_mask(ring, ring_edge, ring_edge);
  if (ring_paths.size() != 2) return fail("a small feathered hole must retain its own contour");
  double ring_error = 0;
  for (const auto& path : ring_paths) {
    for (const auto& value : path.toList()) {
      const auto point = value.toMap();
      const auto radius = std::hypot(point.value(QStringLiteral("x")).toDouble() * (ring_edge - 1) - 58.4,
                                      point.value(QStringLiteral("y")).toDouble() * (ring_edge - 1) - 60.7);
      ring_error = std::max(ring_error, std::min(std::abs(radius - 3.7), std::abs(radius - 30.3)));
    }
  }
  if (ring_error > 0.10) return fail("curved alpha contours and small holes must stay on the sampled midpoint");

  QByteArray islands(80 * 50, '\0');
  for (int index = 0; index < 20; ++index) {
    const int x = 2 + (index % 10) * 7;
    const int y = 2 + (index / 10) * 10;
    fill_rect(&islands, 80, x, y, x + 2, y + 2);
  }
  fill_rect(&islands, 80, 70, 40, 70, 40);
  const auto exact_islands = traced_outline_paths_for_alpha_mask(islands, 80, 50, true);
  if (exact_islands.size() != 21 || traced_outline_paths_for_alpha_mask(islands, 80, 50).size() != 21)
    return fail("more than twelve valid islands and single selected pixels must not disappear");
  const auto exact_rect = traced_outline_paths_for_alpha_mask(rectangular, 5, 5, true).first().toList();
  double left = 1, top = 1, right = 0, bottom = 0;
  for (const auto& value : exact_rect) {
    const auto point = value.toMap();
    left = std::min(left, point.value(QStringLiteral("x")).toDouble());
    top = std::min(top, point.value(QStringLiteral("y")).toDouble());
    right = std::max(right, point.value(QStringLiteral("x")).toDouble());
    bottom = std::max(bottom, point.value(QStringLiteral("y")).toDouble());
  }
  if (!close(left, 1.0 / 5) || !close(top, 2.0 / 5) || !close(right, 4.0 / 5) || !close(bottom, 1))
    return fail("pixel-exact selection outlines must remain on the exact selected cell boundaries");
  std::cout << "diagonal contour max error=" << diagonal_error << " px; curved contour max error=" << ring_error
            << " px; preserved islands=" << exact_islands.size() << '\n';

  return EXIT_SUCCESS;
}
