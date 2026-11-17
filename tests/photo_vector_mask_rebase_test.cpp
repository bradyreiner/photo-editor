#include "photo_vector_mask_rebase.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace desktop = photo_editor::desktop;
namespace masks = photo_editor::desktop::vector_masks;
namespace paths = photo_editor::desktop::vector_paths;

namespace {

bool check(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << "FAIL: " << message << '\n';
  return false;
}

paths::VectorPathDocument local_path() {
  paths::VectorPathDocument path;
  QString error;
  if (!paths::add_subpath(&path, QStringLiteral("outer"), false, &error)) return {};

  paths::PathAnchor a{QStringLiteral("outer-a"), {0.20, 0.25}, {0.0, -0.07}, {0.0, 0.07}, true, paths::AnchorKind::Smooth};
  paths::PathAnchor b{QStringLiteral("outer-b"), {0.73, 0.22}, {-0.06, 0.0}, {0.0, 0.07}, false, paths::AnchorKind::Corner};
  paths::PathAnchor c{QStringLiteral("outer-c"), {0.76, 0.75}, {0.0, -0.07}, {-0.06, 0.0}, false, paths::AnchorKind::Corner};
  paths::PathAnchor d{QStringLiteral("outer-d"), {0.21, 0.78}, {0.06, 0.0}, {0.0, -0.07}, false, paths::AnchorKind::Corner};
  if (!paths::append_anchor(&path, QStringLiteral("outer"), a, &error) ||
      !paths::append_anchor(&path, QStringLiteral("outer"), b, &error) ||
      !paths::append_anchor(&path, QStringLiteral("outer"), c, &error) ||
      !paths::append_anchor(&path, QStringLiteral("outer"), d, &error) ||
      !paths::set_closed(&path, QStringLiteral("outer"), true, &error) ||
      !paths::add_subpath(&path, QStringLiteral("hole"), false, &error)) {
    return {};
  }

  const paths::PathAnchor h0{QStringLiteral("hole-a"), {0.42, 0.43}, {}, {}, false, paths::AnchorKind::Corner};
  const paths::PathAnchor h1{QStringLiteral("hole-b"), {0.59, 0.43}, {}, {}, false, paths::AnchorKind::Corner};
  const paths::PathAnchor h2{QStringLiteral("hole-c"), {0.59, 0.60}, {}, {}, false, paths::AnchorKind::Corner};
  const paths::PathAnchor h3{QStringLiteral("hole-d"), {0.42, 0.60}, {}, {}, false, paths::AnchorKind::Corner};
  if (!paths::append_anchor(&path, QStringLiteral("hole"), h0, &error) ||
      !paths::append_anchor(&path, QStringLiteral("hole"), h1, &error) ||
      !paths::append_anchor(&path, QStringLiteral("hole"), h2, &error) ||
      !paths::append_anchor(&path, QStringLiteral("hole"), h3, &error) ||
      !paths::set_closed(&path, QStringLiteral("hole"), true, &error)) {
    return {};
  }
  return path;
}

masks::PhotoVectorMask local_mask() {
  masks::PhotoVectorMask mask;
  mask.id = QStringLiteral("product-vector-mask");
  mask.enabled = true;
  mask.inverted = true;
  mask.density = 0.63;
  mask.path = local_path();
  return mask;
}

desktop::VectorMaskPlacement crop_group_placement(const QSize& source_size) {
  const QVariantMap group{
      {QStringLiteral("id"), QStringLiteral("group-1")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("x"), 0},
      {QStringLiteral("y"), 0},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("rotation"), 2.0},
      {QStringLiteral("canvasWidth"), source_size.width()},
      {QStringLiteral("canvasHeight"), source_size.height()}};
  const QVariantMap layer{
      {QStringLiteral("id"), QStringLiteral("pixel-1")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("parentId"), QStringLiteral("group-1")},
      {QStringLiteral("x"), 50},
      {QStringLiteral("y"), 40},
      {QStringLiteral("pixelWidth"), 200},
      {QStringLiteral("pixelHeight"), 160},
      {QStringLiteral("scale"), 0.9},
      {QStringLiteral("rotation"), 7.0},
      {QStringLiteral("canvasWidth"), source_size.width()},
      {QStringLiteral("canvasHeight"), source_size.height()}};
  const QVariantList layers{layer, group};
  const desktop::DesktopSourceGeometry geometry{
      source_size, QRect(50, 30, 400, 320)};
  return desktop::vector_mask_placement(layer, layers, geometry);
}

QImage rendered_coverage(const masks::PhotoVectorMask& mask,
                         const desktop::VectorMaskPlacement& placement,
                         const QSize& source_size) {
  const auto path = paths::to_painter_path(mask.path, QSize(1, 1));
  if (path.isEmpty()) return {};

  QTransform source_pixel_transform;
  if (mask.basis == masks::CoordinateBasis::LocalLayer) {
    source_pixel_transform = placement.local_to_source;
  } else {
    source_pixel_transform = QTransform(
        static_cast<double>(source_size.width()), 0.0, 0.0,
        static_cast<double>(source_size.height()), 0.0, 0.0);
  }

  QImage result(source_size, QImage::Format_ARGB32_Premultiplied);
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setTransform(source_pixel_transform);
  painter.fillPath(path, Qt::white);
  painter.end();
  for (int y = 0; y < result.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      const int alpha = qAlpha(row[x]);
      const int oriented = mask.inverted ? 255 - alpha : alpha;
      const int coverage = mask.enabled
          ? 255 - static_cast<int>(std::lround(
                static_cast<double>(255 - oriented) * mask.density))
          : 255;
      row[x] = qRgba(coverage, coverage, coverage, coverage);
    }
  }
  return result;
}

bool nearly_equal(const QPointF& left, const QPointF& right,
                  double tolerance = 1e-10) {
  return std::abs(left.x() - right.x()) <= tolerance &&
      std::abs(left.y() - right.y()) <= tolerance;
}

bool same_ids_and_flags(const paths::VectorPathDocument& left,
                        const paths::VectorPathDocument& right) {
  if (left.subpaths.size() != right.subpaths.size()) return false;
  for (qsizetype i = 0; i < left.subpaths.size(); ++i) {
    const auto& lhs = left.subpaths.at(i);
    const auto& rhs = right.subpaths.at(i);
    if (lhs.id != rhs.id || lhs.closed != rhs.closed ||
        lhs.anchors.size() != rhs.anchors.size()) return false;
    for (qsizetype j = 0; j < lhs.anchors.size(); ++j) {
      const auto& a = lhs.anchors.at(j);
      const auto& b = rhs.anchors.at(j);
      if (a.id != b.id || a.kind != b.kind ||
          a.linked_handles != b.linked_handles) return false;
    }
  }
  return true;
}

bool geometric_equivalence_and_roundtrip() {
  const QSize source_size(500, 400);
  const auto placement = crop_group_placement(source_size);
  if (!check(placement.valid,
             "derive a valid crop-aware placement through a rotated parent group")) return false;

  const auto original = local_mask();
  if (!check(masks::validate(original).isEmpty(),
             "start from a valid editable mask with cubic handles and a hole")) return false;

  masks::PhotoVectorMask fixed;
  QString error;
  if (!check(masks::rebase_coordinates(original,
              masks::CoordinateBasis::SourceDocument, placement, source_size,
              &fixed, &error) && error.isEmpty(),
             "convert a local mask to fixed source-document coordinates")) return false;
  if (!check(fixed.basis == masks::CoordinateBasis::SourceDocument &&
                 fixed.id == original.id && fixed.enabled == original.enabled &&
                 fixed.inverted == original.inverted &&
                 std::abs(fixed.density - original.density) < 1e-12 &&
                 fixed.path.coordinate_space == paths::CoordinateSpace::Normalized &&
                 fixed.path.document_size == QSizeF(0.0, 0.0) &&
                 same_ids_and_flags(original.path, fixed.path),
             "preserve mask settings, subpath and anchor identity, and handle modes")) return false;

  for (qsizetype s = 0; s < original.path.subpaths.size(); ++s) {
    for (qsizetype a = 0; a < original.path.subpaths.at(s).anchors.size(); ++a) {
      const auto& before = original.path.subpaths.at(s).anchors.at(a);
      const auto& after = fixed.path.subpaths.at(s).anchors.at(a);
      const auto map_to_document = [&](const QPointF& point) {
        const auto pixel = placement.local_to_source.map(point);
        return QPointF(pixel.x() / source_size.width(),
                       pixel.y() / source_size.height());
      };
      if (!check(nearly_equal(after.position, map_to_document(before.position)) &&
                     nearly_equal(after.position + after.incoming,
                                  map_to_document(before.position + before.incoming)) &&
                     nearly_equal(after.position + after.outgoing,
                                  map_to_document(before.position + before.outgoing)),
                 "map anchors and both cubic control points through the layer placement")) return false;
    }
  }

  const auto before_coverage = rendered_coverage(original, placement, source_size);
  const auto fixed_coverage = rendered_coverage(fixed, placement, source_size);
  if (!check(!before_coverage.isNull() && !fixed_coverage.isNull(),
             "rasterize both coordinate bases through the same source-pixel frame")) return false;
  int max_difference = 0;
  int differing_pixels = 0;
  for (int y = 0; y < source_size.height(); ++y) {
    for (int x = 0; x < source_size.width(); ++x) {
      const int difference = std::abs(
          before_coverage.pixelColor(x, y).alpha() -
          fixed_coverage.pixelColor(x, y).alpha());
      max_difference = std::max(max_difference, difference);
      if (difference > 2) ++differing_pixels;
    }
  }
  if (!check(max_difference <= 2 && differing_pixels == 0,
             "preserve rendered mask coverage through crop and nested-group transforms")) return false;

  masks::PhotoVectorMask returned;
  if (!check(masks::rebase_coordinates(fixed, masks::CoordinateBasis::LocalLayer,
              placement, source_size, &returned, &error) && error.isEmpty() &&
                 returned.basis == original.basis &&
                 same_ids_and_flags(original.path, returned.path),
             "convert the fixed mask back to its original layer basis")) return false;
  for (qsizetype s = 0; s < original.path.subpaths.size(); ++s) {
    for (qsizetype a = 0; a < original.path.subpaths.at(s).anchors.size(); ++a) {
      const auto& before = original.path.subpaths.at(s).anchors.at(a);
      const auto& after = returned.path.subpaths.at(s).anchors.at(a);
      if (!check(nearly_equal(before.position, after.position) &&
                     nearly_equal(before.incoming, after.incoming) &&
                     nearly_equal(before.outgoing, after.outgoing),
                 "round-trip anchor and handle geometry without baking it")) return false;
    }
  }

  // The schema also permits document-coordinate paths. Their declared size is
  // independent of the mask's selected basis, so conversion must normalize
  // them before applying the layer/source transform.
  auto document_path_mask = fixed;
  document_path_mask.path.coordinate_space = paths::CoordinateSpace::Document;
  document_path_mask.path.document_size = QSizeF(source_size);
  for (auto& subpath : document_path_mask.path.subpaths) {
    for (auto& anchor : subpath.anchors) {
      anchor.position = {anchor.position.x() * source_size.width(),
                         anchor.position.y() * source_size.height()};
      anchor.incoming = {anchor.incoming.x() * source_size.width(),
                         anchor.incoming.y() * source_size.height()};
      anchor.outgoing = {anchor.outgoing.x() * source_size.width(),
                         anchor.outgoing.y() * source_size.height()};
    }
  }
  masks::PhotoVectorMask from_document_path;
  if (!check(masks::rebase_coordinates(document_path_mask,
              masks::CoordinateBasis::LocalLayer, placement, source_size,
              &from_document_path, &error) && error.isEmpty() &&
                 from_document_path.basis == masks::CoordinateBasis::LocalLayer,
             "convert a valid document-coordinate path as well as a normalized path")) return false;
  return true;
}

bool failures_are_atomic() {
  const QSize source_size(500, 400);
  const auto placement = crop_group_placement(source_size);
  auto sentinel = local_mask();
  sentinel.id = QStringLiteral("untouched-output");
  const auto sentinel_serialized = masks::serialize(sentinel);
  auto output = sentinel;
  auto invalid = local_mask();
  invalid.id.clear();
  QString error;
  if (!check(!masks::rebase_coordinates(invalid,
              masks::CoordinateBasis::SourceDocument, placement, source_size,
              &output, &error) && !error.isEmpty() &&
                 masks::serialize(output) == sentinel_serialized,
             "reject invalid source definitions without partially changing output")) return false;

  auto outside = placement;
  outside.local_to_source = QTransform(1.0, 0.0, 0.0, 1.0, -200.0, 0.0);
  outside.valid = true;
  output = sentinel;
  if (!check(!masks::rebase_coordinates(local_mask(),
              masks::CoordinateBasis::SourceDocument, outside, source_size,
              &output, &error) && !error.isEmpty() &&
                 masks::serialize(output) == sentinel_serialized,
             "reject out-of-document anchors and preserve the previous output")) return false;

  auto unavailable = placement;
  unavailable.valid = false;
  output = sentinel;
  if (!check(!masks::rebase_coordinates(local_mask(),
              masks::CoordinateBasis::SourceDocument, unavailable, source_size,
              &output, &error) && !error.isEmpty() &&
                 masks::serialize(output) == sentinel_serialized,
             "reject an unavailable placement without changing output")) return false;

  return check(!masks::rebase_coordinates(local_mask(),
                    masks::CoordinateBasis::SourceDocument, placement, {},
                    &output, &error) && !error.isEmpty() &&
                   masks::serialize(output) == sentinel_serialized,
               "reject invalid source dimensions atomically");
}

}  // namespace

int main() {
  if (!geometric_equivalence_and_roundtrip() || !failures_are_atomic()) return 1;
  std::cout << "photo_vector_mask_rebase_test passed\n";
  return 0;
}
