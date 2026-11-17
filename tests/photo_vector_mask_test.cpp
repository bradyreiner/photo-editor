#include "photo_vector_mask.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <iostream>
#include <limits>

namespace masks = photo_editor::desktop::vector_masks;
namespace paths = photo_editor::desktop::vector_paths;

namespace {

bool check(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << "FAIL: " << message << '\n';
  return false;
}

paths::PathAnchor anchor(const QString& id, QPointF position,
                         QPointF incoming = {}, QPointF outgoing = {}) {
  paths::PathAnchor result;
  result.id = id;
  result.position = position;
  result.incoming = incoming;
  result.outgoing = outgoing;
  return result;
}

bool append(paths::VectorPathDocument* document, const QString& subpath_id,
            const paths::PathAnchor& value) {
  QString error;
  return paths::append_anchor(document, subpath_id, value, &error);
}

paths::VectorPathDocument rectangle(const QString& id, double left = 0.25,
                                    double top = 0.25, double right = 0.75,
                                    double bottom = 0.75) {
  paths::VectorPathDocument document;
  paths::add_subpath(&document, id);
  append(&document, id, anchor(id + QStringLiteral("-a"), {left, top}));
  append(&document, id, anchor(id + QStringLiteral("-b"), {right, top}));
  append(&document, id, anchor(id + QStringLiteral("-c"), {right, bottom}));
  append(&document, id, anchor(id + QStringLiteral("-d"), {left, bottom}));
  paths::set_closed(&document, id, true);
  return document;
}

masks::PhotoVectorMask valid_mask() {
  masks::PhotoVectorMask mask;
  mask.id = QStringLiteral("product-cutout");
  mask.path = rectangle(QStringLiteral("outline"));
  auto& first = mask.path.subpaths[0].anchors[0];
  first.outgoing = {0.0, 0.12};
  auto& second = mask.path.subpaths[0].anchors[1];
  second.incoming = {0.0, -0.12};
  return mask;
}

int alpha_at(const QImage& image, int x, int y) {
  return image.pixelColor(x, y).alpha();
}

bool persistence_roundtrip() {
  auto mask = valid_mask();
  mask.basis = masks::CoordinateBasis::SourceDocument;
  mask.enabled = false;
  mask.inverted = true;
  mask.density = 0.375;
  QString error;
  const auto bytes = masks::serialize(mask, &error);
  if (!check(!bytes.isEmpty() && error.isEmpty(),
             "serialize a validated vector mask definition")) return false;
  masks::PhotoVectorMask decoded;
  if (!check(masks::deserialize(bytes, &decoded, &error) && error.isEmpty() &&
                 masks::serialize(decoded) == bytes,
             "preserve exact editable path and mask settings on recipe roundtrip")) return false;
  if (!check(decoded.id == mask.id && decoded.basis == mask.basis &&
                 !decoded.enabled && decoded.inverted &&
                 std::abs(decoded.density - mask.density) < 1e-12 &&
                 decoded.path.subpaths[0].anchors[0].outgoing ==
                     QPointF(0.0, 0.12),
             "retain explicit basis and cubic handle geometry")) return false;

  masks::PhotoVectorMask variant_decoded;
  if (!check(masks::from_variant(masks::to_variant(mask), &variant_decoded, &error) &&
                 masks::serialize(variant_decoded) == bytes,
             "retain the same definition through QVariantMap persistence")) return false;
  return true;
}

bool strict_validation() {
  auto mask = valid_mask();
  QString error;
  auto object = masks::to_json(mask, &error);
  object.insert(QStringLiteral("version"), 7);
  masks::PhotoVectorMask untouched = valid_mask();
  const auto unchanged_id = untouched.id;
  if (!check(!masks::from_json(object, &untouched, &error) &&
                 untouched.id == unchanged_id,
             "reject unsupported schema versions without partial output mutation")) return false;

  object = masks::to_json(mask);
  object.insert(QStringLiteral("basis"), QStringLiteral("unknown-space"));
  if (!check(!masks::from_json(object, &untouched, &error),
             "reject unknown coordinate bases")) return false;

  object = masks::to_json(mask);
  object.insert(QStringLiteral("unowned"), true);
  if (!check(!masks::from_json(object, &untouched, &error),
             "reject extra fields instead of ignoring unsupported data")) return false;

  mask.density = std::numeric_limits<double>::quiet_NaN();
  if (!check(!masks::validate(mask).isEmpty() &&
                 masks::rasterize_mask(mask, QSize(32, 32), &error).isNull(),
             "reject non-finite mask density")) return false;

  mask = valid_mask();
  mask.path.subpaths[0].closed = false;
  if (!check(!masks::validate(mask).isEmpty() &&
                 masks::rasterize_mask(mask, QSize(32, 32), &error).isNull(),
             "reject open contours as masks instead of rendering full coverage")) return false;

  mask = valid_mask();
  mask.path.version = paths::kVectorPathSchemaVersion + 1;
  mask.enabled = false;
  if (!check(masks::rasterize_mask(mask, QSize(32, 32), &error).isNull() &&
                 !error.isEmpty(),
             "do not bypass invalid or unsupported definitions as full coverage")) return false;
  return true;
}

bool scalable_coverage_and_density() {
  auto mask = valid_mask();
  QString error;
  const auto small = masks::rasterize_mask(mask, QSize(64, 64), &error);
  const auto large = masks::rasterize_mask(mask, QSize(256, 256), &error);
  if (!check(!small.isNull() && !large.isNull() && error.isEmpty() &&
                 alpha_at(small, 32, 32) == 255 &&
                 alpha_at(small, 4, 32) == 0 &&
                 alpha_at(large, 128, 128) == 255 &&
                 alpha_at(large, 16, 128) == 0,
             "rasterize the editable contour at multiple requested resolutions")) return false;

  mask.density = 0.5;
  const auto reduced = masks::rasterize_mask(mask, QSize(64, 64), &error);
  if (!check(alpha_at(reduced, 32, 32) == 255 &&
                 alpha_at(reduced, 4, 32) >= 127 &&
                 alpha_at(reduced, 4, 32) <= 128,
             "reduce mask effect toward full coverage with standard density semantics")) return false;

  mask.inverted = true;
  mask.density = 1.0;
  const auto inverted = masks::rasterize_mask(mask, QSize(64, 64), &error);
  if (!check(alpha_at(inverted, 32, 32) == 0 &&
                 alpha_at(inverted, 4, 32) == 255,
             "invert antialiased mask coverage")) return false;

  mask.inverted = false;
  mask.density = 0.0;
  const auto zero_density = masks::rasterize_mask(mask, QSize(64, 64), &error);
  if (!check(alpha_at(zero_density, 32, 32) == 255 &&
                 alpha_at(zero_density, 4, 32) == 255,
             "treat zero density as a disabled all-white coverage effect")) return false;

  mask.density = 1.0;
  mask.enabled = false;
  const auto bypassed = masks::rasterize_mask(mask, QSize(64, 64), &error);
  if (!check(alpha_at(bypassed, 32, 32) == 255 &&
                 alpha_at(bypassed, 4, 32) == 255,
             "bypass a valid mask with full coverage")) return false;
  return true;
}

bool closed_contours_and_holes() {
  auto mask = valid_mask();
  const auto hole = rectangle(QStringLiteral("hole"), 0.4, 0.4, 0.6, 0.6);
  mask.path.subpaths.append(hole.subpaths.first());
  QString error;
  const auto raster = masks::rasterize_mask(mask, QSize(100, 100), &error);
  if (!check(!raster.isNull() && error.isEmpty() &&
                 alpha_at(raster, 30, 50) == 255 &&
                 alpha_at(raster, 50, 50) == 0,
             "preserve closed even-odd contours and holes as editable vector geometry")) return false;

  mask.path.subpaths[1].closed = false;
  if (!check(masks::rasterize_mask(mask, QSize(100, 100), &error).isNull(),
             "require every contour in the mask document to be closed")) return false;
  if (!check(masks::rasterize_mask(valid_mask(), QSize(100000, 2), &error).isNull(),
             "bound vector mask raster allocation")) return false;
  return true;
}

}  // namespace

int main() {
  if (!persistence_roundtrip() || !strict_validation() ||
      !scalable_coverage_and_density() || !closed_contours_and_holes())
    return 1;
  std::cout << "photo_vector_mask_test passed\n";
  return 0;
}
