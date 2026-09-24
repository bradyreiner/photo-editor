#include "mask_outline_tracing.h"

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
  if (component_paths.size() != 2 ||
      component_paths != traced_outline_paths_for_alpha_mask(components, 30, 20)) {
    return fail("outline tracing must retain material loops, suppress noise, and remain deterministic");
  }

  QByteArray edge_filled(12, static_cast<char>(255));
  const auto edge_paths =
      traced_outline_paths_for_alpha_mask_with_edge_padding(edge_filled, 4, 3);
  if (edge_paths.size() != 1 || !normalized_path(edge_paths.first().toList())) {
    return fail("edge-touching alpha masks must retain a bounded closed outline");
  }

  return EXIT_SUCCESS;
}
