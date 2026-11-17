#include "photo_vector_path.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <iostream>
#include <limits>

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

paths::VectorPathDocument rectangle(const QString& subpath_id, double left,
                                    double top, double right, double bottom) {
  paths::VectorPathDocument document;
  paths::add_subpath(&document, subpath_id);
  append(&document, subpath_id, anchor(subpath_id + QStringLiteral("-a"), {left, top}));
  append(&document, subpath_id, anchor(subpath_id + QStringLiteral("-b"), {right, top}));
  append(&document, subpath_id, anchor(subpath_id + QStringLiteral("-c"), {right, bottom}));
  append(&document, subpath_id, anchor(subpath_id + QStringLiteral("-d"), {left, bottom}));
  paths::set_closed(&document, subpath_id, true);
  return document;
}

bool cubic_and_scaling() {
  paths::VectorPathDocument document;
  QString error;
  if (!check(paths::add_subpath(&document, QStringLiteral("curve"), false, &error),
             "add an open subpath")) return false;
  if (!check(append(&document, QStringLiteral("curve"),
                    anchor(QStringLiteral("start"), {0.1, 0.5}, {}, {0.0, 0.4})),
             "append first cubic anchor")) return false;
  if (!check(append(&document, QStringLiteral("curve"),
                    anchor(QStringLiteral("end"), {0.9, 0.5}, {0.0, -0.4}, {})),
             "append second cubic anchor")) return false;
  const auto path = paths::to_painter_path(document, QSize(200, 100), &error);
  if (!check(error.isEmpty() && path.elementCount() >= 4,
             "convert cubic anchors into native path geometry")) return false;
  if (!check(path.elementAt(1).type == QPainterPath::CurveToElement,
             "retain cubic curve segments")) return false;
  const auto bounds = path.boundingRect();
  if (!check(std::abs(bounds.left() - 20.0) < 1e-8 &&
             std::abs(bounds.right() - 180.0) < 1e-8 && bounds.height() > 20.0,
             "scale normalized points and handles to requested output dimensions")) return false;

  document.coordinate_space = paths::CoordinateSpace::Document;
  document.document_size = QSizeF(100.0, 50.0);
  document.subpaths[0].anchors[0].position = {10.0, 25.0};
  document.subpaths[0].anchors[0].outgoing = {0.0, 20.0};
  document.subpaths[0].anchors[1].position = {90.0, 25.0};
  document.subpaths[0].anchors[1].incoming = {0.0, -20.0};
  const auto document_path = paths::to_painter_path(document, QSize(200, 100), &error);
  if (!check(error.isEmpty() && std::abs(document_path.boundingRect().left() - 20.0) < 1e-8 &&
             std::abs(document_path.boundingRect().right() - 180.0) < 1e-8 &&
             document_path.boundingRect().height() > 20.0,
             "scale document coordinates to target resolution")) return false;
  return true;
}

bool mutation_and_recipe_roundtrip() {
  paths::VectorPathDocument document;
  QString error;
  if (!check(paths::add_subpath(&document, QStringLiteral("outline"), false, &error),
             "start editable subpath")) return false;
  if (!check(append(&document, QStringLiteral("outline"), anchor(QStringLiteral("a"), {0.1, 0.1})) &&
             append(&document, QStringLiteral("outline"), anchor(QStringLiteral("b"), {0.9, 0.1})) &&
             append(&document, QStringLiteral("outline"), anchor(QStringLiteral("c"), {0.5, 0.9})),
             "add editable anchors")) return false;
  if (!check(paths::set_closed(&document, QStringLiteral("outline"), true, &error),
             "close a three-anchor path")) return false;
  if (!check(paths::set_anchor_kind(&document, QStringLiteral("outline"), QStringLiteral("b"),
                                    paths::AnchorKind::Smooth, &error),
             "convert a corner to smooth")) return false;
  if (!check(paths::set_handle(&document, QStringLiteral("outline"), QStringLiteral("b"),
                               paths::HandleSide::Outgoing, {0.12, 0.08}, &error) &&
             document.subpaths[0].anchors[1].incoming == QPointF(-0.12, -0.08),
             "linked smooth handles move together")) return false;
  const auto before_invalid_remove = paths::serialize(document);
  if (!check(!paths::remove_anchor(&document, QStringLiteral("outline"), QStringLiteral("a"), &error) &&
             paths::serialize(document) == before_invalid_remove,
             "reject invalid edits transactionally without changing a closed path")) return false;
  if (!check(paths::move_anchor(&document, QStringLiteral("outline"), QStringLiteral("a"),
                                {0.2, 0.15}, &error) &&
             paths::set_anchor_kind(&document, QStringLiteral("outline"), QStringLiteral("b"),
                                    paths::AnchorKind::Corner, &error) &&
             paths::set_closed(&document, QStringLiteral("outline"), false, &error) &&
             paths::remove_anchor(&document, QStringLiteral("outline"), QStringLiteral("a"), &error),
             "move, convert, open, and remove anchors")) return false;

  paths::VectorPathDocument recipe = rectangle(QStringLiteral("main"), 0.1, 0.15, 0.9, 0.85);
  const auto bytes = paths::serialize(recipe, &error);
  if (!check(!bytes.isEmpty() && error.isEmpty(), "serialize versioned path recipe")) return false;
  paths::VectorPathDocument decoded;
  if (!check(paths::deserialize(bytes, &decoded, &error) &&
             paths::serialize(decoded) == bytes,
             "preserve exact canonical recipe bytes after reopen")) return false;
  paths::VectorPathDocument from_map;
  if (!check(paths::to_document(paths::to_variant(recipe), &from_map, &error) &&
             paths::serialize(from_map) == bytes,
             "adapt the editable recipe through QVariantMap")) return false;
  return true;
}

bool validation_and_finite_guards() {
  paths::VectorPathDocument document = rectangle(QStringLiteral("valid"), 0.1, 0.1, 0.9, 0.9);
  QString error;
  const auto before = paths::serialize(document);
  if (!check(!paths::move_anchor(&document, QStringLiteral("valid"), QStringLiteral("valid-a"),
                                 {std::numeric_limits<double>::infinity(), 0.5}, &error) &&
             paths::serialize(document) == before,
             "reject non-finite coordinates without partial mutation")) return false;
  if (!check(!paths::set_handle(&document, QStringLiteral("valid"), QStringLiteral("valid-a"),
                                paths::HandleSide::Incoming, {5.0, 0.0}, &error),
             "reject out-of-range normalized handles")) return false;
  auto malformed = paths::to_json(document);
  malformed.insert(QStringLiteral("version"), 77);
  paths::VectorPathDocument ignored;
  if (!check(!paths::from_json(malformed, &ignored, &error),
             "reject unsupported recipe versions")) return false;
  malformed = paths::to_json(document);
  malformed.insert(QStringLiteral("unownedField"), true);
  if (!check(!paths::from_json(malformed, &ignored, &error),
             "reject unexpected recipe fields")) return false;
  malformed = paths::to_json(document);
  auto subpaths = malformed.value(QStringLiteral("subpaths")).toArray();
  auto subpath = subpaths.at(0).toObject();
  auto anchors = subpath.value(QStringLiteral("anchors")).toArray();
  auto first = anchors.at(0).toObject();
  first.insert(QStringLiteral("point"), QJsonArray{std::numeric_limits<double>::quiet_NaN(), 0.1});
  anchors.replace(0, first);
  subpath.insert(QStringLiteral("anchors"), anchors);
  subpaths.replace(0, subpath);
  malformed.insert(QStringLiteral("subpaths"), subpaths);
  if (!check(!paths::from_json(malformed, &ignored, &error),
             "reject malformed non-finite recipe coordinates")) return false;
  if (!check(paths::rasterize_mask(document, QSize(100000, 2), &error).isNull(),
             "guard excessive raster dimensions")) return false;
  return true;
}

bool holes_boolean_and_raster() {
  paths::VectorPathDocument open_contour;
  QString error;
  if (!check(paths::add_subpath(&open_contour, QStringLiteral("open"), false, &error) &&
             append(&open_contour, QStringLiteral("open"), anchor(QStringLiteral("open-a"), {0.15, 0.15})) &&
             append(&open_contour, QStringLiteral("open"), anchor(QStringLiteral("open-b"), {0.85, 0.15})) &&
             append(&open_contour, QStringLiteral("open"), anchor(QStringLiteral("open-c"), {0.5, 0.85})),
             "create a three-anchor open contour")) return false;
  const auto open_fill = paths::rasterize_fill(open_contour, QSize(200, 200), Qt::white, &error);
  const auto open_stroke = paths::rasterize_stroke(open_contour, QSize(200, 200), Qt::white, 2.0, &error);
  if (!check(!open_fill.isNull() && open_fill.pixelColor(100, 90).alpha() == 0 &&
             !open_stroke.isNull() && open_stroke.pixelColor(100, 30).alpha() > 0,
             "keep open contours stroke-only while preserving their visible outline")) return false;
  if (!check(paths::set_closed(&open_contour, QStringLiteral("open"), true, &error),
             "close the open contour explicitly")) return false;
  const auto closed_fill = paths::rasterize_fill(open_contour, QSize(200, 200), Qt::white, &error);
  if (!check(!closed_fill.isNull() && closed_fill.pixelColor(100, 90).alpha() > 0,
             "fill the same contour after it is explicitly closed")) return false;

  auto donut = rectangle(QStringLiteral("outer"), 0.1, 0.1, 0.9, 0.9);
  auto inner = rectangle(QStringLiteral("hole"), 0.3, 0.3, 0.7, 0.7);
  donut.subpaths.append(inner.subpaths.first());
  const auto before = paths::serialize(donut);
  const auto mask = paths::rasterize_mask(donut, QSize(200, 200), &error);
  if (!check(!mask.isNull() && error.isEmpty() && mask.pixelColor(40, 100).alpha() == 255 &&
             mask.pixelColor(100, 100).alpha() == 0,
             "rasterize alpha masks with an even-odd hole")) return false;
  const auto left = rectangle(QStringLiteral("left"), 0.1, 0.1, 0.6, 0.9);
  const auto right = rectangle(QStringLiteral("right"), 0.4, 0.1, 0.9, 0.9);
  const auto left_bytes = paths::serialize(left);
  const auto right_bytes = paths::serialize(right);
  const auto united = paths::combine_paths(left, right, paths::BooleanOperation::Union,
                                            QSize(200, 200), &error);
  if (!check(united.contains({40.0, 100.0}) && united.contains({160.0, 100.0}) &&
             united.contains({100.0, 100.0}),
             "union vector operands")) return false;
  const auto subtraction = paths::combine_paths(left, right, paths::BooleanOperation::Subtract,
                                                 QSize(200, 200), &error);
  if (!check(error.isEmpty() && subtraction.contains({40.0, 100.0}) &&
             !subtraction.contains({100.0, 100.0}) &&
             paths::serialize(left) == left_bytes && paths::serialize(right) == right_bytes &&
             paths::serialize(donut) == before,
             "subtract geometry while preserving editable source recipes")) return false;
  const auto intersection = paths::combine_paths(left, right, paths::BooleanOperation::Intersect,
                                                  QSize(200, 200), &error);
  if (!check(intersection.contains({100.0, 100.0}) && !intersection.contains({40.0, 100.0}),
             "intersect independently editable path operands")) return false;
  const auto exclusion = paths::combine_paths(left, right, paths::BooleanOperation::Exclude,
                                               QSize(200, 200), &error);
  if (!check(exclusion.contains({40.0, 100.0}) && !exclusion.contains({100.0, 100.0}) &&
             exclusion.contains({160.0, 100.0}),
             "exclude overlap between vector operands")) return false;
  const auto stroke = paths::rasterize_stroke(left, QSize(200, 200), Qt::white, 3.0, &error);
  if (!check(!stroke.isNull() && stroke.pixelColor(20, 100).alpha() > 0,
             "rasterize antialiased strokes at requested resolution")) return false;
  return true;
}

}  // namespace

int main() {
  if (!cubic_and_scaling() || !mutation_and_recipe_roundtrip() ||
      !validation_and_finite_guards() || !holes_boolean_and_raster())
    return 1;
  std::cout << "photo_vector_path_test passed\n";
  return 0;
}
