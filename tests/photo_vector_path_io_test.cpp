#include "photo_vector_path_io.h"

#include <QPainterPath>

#include <cmath>
#include <iostream>

namespace {

namespace paths = photo_editor::desktop::vector_paths;
namespace io = photo_editor::desktop::vector_path_io;

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool close(double lhs, double rhs, double tolerance = 1.0e-8) {
  return std::abs(lhs - rhs) <= tolerance;
}

bool import_svg_document(const QByteArray& bytes,
                         QVector<io::StyledPath>* output,
                         QString* error = nullptr) {
  return io::import_svg(bytes, output, error);
}

paths::PathAnchor anchor(const QString& id, const QPointF& point,
                         const QPointF& incoming = {}, const QPointF& outgoing = {}) {
  paths::PathAnchor result;
  result.id = id;
  result.position = point;
  result.incoming = incoming;
  result.outgoing = outgoing;
  return result;
}

paths::VectorPathDocument round_trip_document() {
  paths::VectorPathDocument document;
  document.coordinate_space = paths::CoordinateSpace::Document;
  document.document_size = QSizeF(120.0, 80.0);
  paths::VectorSubpath closed;
  closed.id = QStringLiteral("closed");
  closed.closed = true;
  closed.anchors = {
      anchor(QStringLiteral("a"), QPointF(10.0, 10.0), {}, QPointF(0.0, -6.0)),
      anchor(QStringLiteral("b"), QPointF(90.0, 10.0), QPointF(0.0, -6.0), QPointF(6.0, 0.0)),
      anchor(QStringLiteral("c"), QPointF(50.0, 70.0), QPointF(4.0, 0.0), QPointF(-4.0, 0.0))};
  paths::VectorSubpath open;
  open.id = QStringLiteral("open");
  open.anchors = {
      anchor(QStringLiteral("d"), QPointF(20.0, 40.0), {}, QPointF(5.0, -2.0)),
      anchor(QStringLiteral("e"), QPointF(100.0, 45.0), QPointF(-7.0, 3.0), {})};
  document.subpaths = {closed, open};
  return document;
}

bool test_commands_viewbox_transforms_and_styles() {
  const QByteArray svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="10 20 200 100">
    <g transform="translate(5,-5)" fill="#336699" stroke="#ff6600" stroke-width="2">
      <path d="M5 25 H105 V75 L5 75 Z M35 35 l20 0 v20 h-20 z" fill-rule="evenodd"/>
      <path d="M 10 60 q 10 -10 20 0 t 20 0" fill="none" stroke="blue"/>
    </g>
  </svg>)svg";
  QVector<io::StyledPath> imported;
  QString error;
  if (!check(import_svg_document(svg, &imported, &error), "Import supported paths with nested styles and transforms")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!check(imported.size() == 2, "Preserve multiple SVG path elements")) return false;
  const auto& filled = imported.at(0);
  if (!check(filled.path.coordinate_space == paths::CoordinateSpace::Document &&
                 filled.path.document_size == QSizeF(200.0, 100.0),
             "Keep the imported viewBox dimensions as the editable document basis") ||
      !check(filled.path.subpaths.size() == 2 && filled.path.subpaths.at(0).closed &&
                 filled.path.subpaths.at(1).closed,
             "Preserve multiple closed contours in a single SVG path") ||
      !check(filled.path.subpaths.at(0).anchors.first().position == QPointF(0.0, 0.0),
             "Apply group translation and subtract viewBox origin") ||
      !check(filled.fill_color == QStringLiteral("#336699") &&
                 filled.stroke_color == QStringLiteral("#ff6600") &&
                 close(filled.stroke_width, 2.0),
             "Inherit solid fill and stroke styling across SVG groups")) return false;
  const auto& open = imported.at(1);
  if (!check(open.path.subpaths.size() == 1 && !open.path.subpaths.first().closed &&
                 open.path.subpaths.first().anchors.size() == 3,
             "Convert quadratic Q/T segments into editable cubic anchors") ||
      !check(open.fill_color.isEmpty() && open.stroke_color == QStringLiteral("#0000ff"),
             "Preserve no-fill and named stroke colors")) return false;
  const auto& first = open.path.subpaths.first().anchors;
  if (!check(!first.at(0).outgoing.isNull() && !first.at(1).incoming.isNull() &&
                 !first.at(1).outgoing.isNull() && !first.at(2).incoming.isNull(),
             "Store converted quadratic handles on editable anchors")) return false;
  return true;
}

bool test_relative_smooth_and_alpha() {
  const QByteArray svg = R"svg(<svg viewBox="0 0 100 100">
    <path d="m10 10 c10 -10 20 -10 30 0 s20 10 30 0 z"
          fill="#ff000080" fill-rule="evenodd" stroke="#0f8" stroke-width="3px"/>
  </svg>)svg";
  QVector<io::StyledPath> imported;
  QString error;
  if (!check(import_svg_document(svg, &imported, &error), "Import relative C/S commands and CSS alpha hex")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!check(imported.size() == 1 && imported.first().path.subpaths.first().closed &&
                 imported.first().path.subpaths.first().anchors.size() == 3,
             "Convert relative smooth cubic path commands into three anchors") ||
      !check(imported.first().fill_color == QStringLiteral("#80ff0000") &&
                 imported.first().stroke_color == QStringLiteral("#00ff88") &&
                 close(imported.first().stroke_width, 3.0),
             "Keep CSS alpha in QColor-compatible path colors")) return false;
  const auto& contour = imported.first().path.subpaths.first();
  const auto expected_reflected = contour.anchors.at(1).position * 2.0 -
                                  (contour.anchors.at(1).position + contour.anchors.at(1).incoming);
  const auto actual_second_control = contour.anchors.at(1).position + contour.anchors.at(1).outgoing;
  if (!check(close(expected_reflected.x(), actual_second_control.x()) &&
                 close(expected_reflected.y(), actual_second_control.y()),
             "Reflect the previous cubic control point for S")) return false;
  return true;
}

bool test_affine_matrix_and_stroke_scaling() {
  const QByteArray svg = R"svg(<svg viewBox="0 0 100 100">
    <path transform="matrix(0 -1 1 0 50 50)"
          d="M20 20 H40 V40 Z" fill="none" stroke="none"/>
    <path transform="translate(10,10) scale(2)"
          d="M0 0 L10 0 L10 10 Z" fill="none" stroke="#ffffff" stroke-width="2"/>
  </svg>)svg";
  QVector<io::StyledPath> imported;
  QString error;
  if (!check(import_svg_document(svg, &imported, &error),
             "Import matrix transforms and uniformly scaled strokes")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  const auto& matrix_path = imported.at(0).path.subpaths.first().anchors;
  if (!check(matrix_path.at(0).position == QPointF(70.0, 30.0) &&
                 matrix_path.at(1).position == QPointF(70.0, 10.0) &&
                 matrix_path.at(2).position == QPointF(90.0, 10.0),
             "Apply the SVG affine matrix before translating by viewBox origin") ||
      !check(imported.at(1).path.subpaths.first().anchors.first().position ==
                 QPointF(10.0, 10.0) &&
                 imported.at(1).path.subpaths.first().anchors.at(1).position ==
                 QPointF(30.0, 10.0) &&
                 close(imported.at(1).stroke_width, 4.0),
             "Compose translate and uniform scale while scaling scalar stroke width")) return false;
  const QByteArray root_transform_svg = R"svg(<svg viewBox="10 5 100 100" transform="scale(2)">
    <path d="M10 15 L20 15" fill="none" stroke="#ffffff" stroke-width="2"/>
  </svg>)svg";
  QVector<io::StyledPath> root_transformed;
  if (!check(import_svg_document(root_transform_svg, &root_transformed, &error),
             "Apply a root transform outside the viewBox coordinate mapping")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  const auto& root_anchors = root_transformed.first().path.subpaths.first().anchors;
  if (!check(root_anchors.at(0).position == QPointF(0.0, 20.0) &&
                 root_anchors.at(1).position == QPointF(20.0, 20.0) &&
                 close(root_transformed.first().stroke_width, 4.0),
             "Map nonzero viewBox origin before applying root scale")) return false;
  return true;
}

bool test_round_trip_preserves_cubics_and_styles() {
  const auto authored = round_trip_document();
  io::StyledPath source;
  source.path = authored;
  source.path.subpaths.removeLast();
  source.fill_color = QStringLiteral("#99224466");
  source.stroke_color = QStringLiteral("#ffcc00");
  source.stroke_width = 4.5;
  io::StyledPath open_source;
  open_source.path = authored;
  open_source.path.subpaths = {authored.subpaths.last()};
  open_source.stroke_color = QStringLiteral("#ffffff");
  open_source.stroke_width = 2.0;
  QString error;
  const auto bytes = io::export_svg({source, open_source}, source.path.document_size, &error);
  if (!check(!bytes.isEmpty(), "Export editable closed and open contours")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!check(bytes.contains("fill-rule=\"evenodd\""),
             "Declare the supported fill rule on every exported path")) return false;
  QVector<io::StyledPath> reopened;
  if (!check(import_svg_document(bytes, &reopened, &error), "Reimport the emitted path-only SVG")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!check(reopened.size() == 2 && reopened.first().path.subpaths.size() == 1 &&
                 reopened.last().path.subpaths.size() == 1,
             "Round-trip all contours as editable paths") ||
      !check(reopened.first().path.subpaths.first().closed &&
                 !reopened.last().path.subpaths.first().closed,
             "Round-trip open and closed subpath state") ||
      !check(reopened.first().fill_color == source.fill_color &&
                 reopened.first().stroke_color == source.stroke_color &&
                 close(reopened.first().stroke_width, source.stroke_width),
             "Round-trip fill, alpha, stroke and width") ||
      !check(reopened.last().fill_color.isEmpty() &&
                 reopened.last().stroke_color == open_source.stroke_color &&
                 close(reopened.last().stroke_width, open_source.stroke_width),
             "Round-trip an open stroke-only path separately from the filled contour")) return false;
  const auto before = paths::to_painter_path(source.path, QSize(120, 80), &error);
  const auto after = paths::to_painter_path(reopened.first().path, QSize(120, 80), &error);
  if (!check(before.elementCount() == after.elementCount(),
             "Round-trip keeps the cubic path element count") ||
      !check(before == after, "Round-trip preserves exact cubic path geometry")) return false;
  const auto open_before = paths::to_painter_path(open_source.path, QSize(120, 80), &error);
  const auto open_after = paths::to_painter_path(reopened.last().path, QSize(120, 80), &error);
  if (!check(open_before == open_after, "Round-trip preserves open cubic path geometry")) return false;
  return true;
}

bool test_nonzero_semantics_and_unsupported_features() {
  QVector<io::StyledPath> imported;
  QString error;
  const QByteArray simple_nonzero = R"svg(<svg viewBox="0 0 100 100"><path d="M10 10 H90 V90 H10 Z"/></svg>)svg";
  if (!check(import_svg_document(simple_nonzero, &imported, &error),
             "Accept default nonzero fill when it renders identically to evenodd")) return false;
  const QByteArray same_winding_hole = R"svg(<svg viewBox="0 0 100 100"><path d="M0 0 H90 V90 H0 Z M20 20 H70 V70 H20 Z"/></svg>)svg";
  if (!check(!import_svg_document(same_winding_hole, &imported, &error) && error.contains(QStringLiteral("nonzero fill-rule")),
             "Reject nonzero fills whose holes differ from the editable model")) return false;
  const QByteArray opposite_winding_hole = R"svg(<svg viewBox="0 0 100 100"><path d="M0 0 H90 V90 H0 Z M20 20 V70 H70 V20 Z"/></svg>)svg";
  if (!check(import_svg_document(opposite_winding_hole, &imported, &error),
             "Accept a nonzero hole when the evenodd renderer produces the same filled region")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }

  const QVector<QPair<QByteArray, QString>> rejected{
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><path d='M0 0 A 2 2 0 0 1 4 4'/></svg>"), QStringLiteral("arc commands")},
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><script>alert(1)</script></svg>"), QStringLiteral("Unsupported SVG element")},
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><image href='https://example.invalid/a.png'/></svg>"), QStringLiteral("Unsupported SVG element")},
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><path d='M1 1 L9 1 L9 9' fill='red'/></svg>"), QStringLiteral("implicitly close open contours")},
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><path d='M1 1 L9 1 L9 9 Z' filter='url(#blur)'/></svg>"), QStringLiteral("Unsupported SVG attribute")},
      {QByteArrayLiteral("<svg viewBox='0 0 10 10'><path d='M1 1 L9 1 L9 9 Z' style='fill:url(https://example.invalid/x)'/></svg>"), QStringLiteral("Functional SVG colors")},
      {QByteArrayLiteral("<!DOCTYPE svg [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><svg viewBox='0 0 10 10'>&x;</svg>"), QStringLiteral("document types")}};
  for (const auto& test : rejected) {
    if (!check(!import_svg_document(test.first, &imported, &error) && error.contains(test.second),
               "Reject unsupported SVG commands, references, filters and active content")) {
      std::cerr << error.toStdString() << '\n';
      return false;
    }
  }
  QByteArray too_large(4 * 1024 * 1024 + 1, ' ');
  if (!check(!import_svg_document(too_large, &imported, &error) && error.contains(QStringLiteral("4 MiB")),
             "Bound SVG input size before XML parsing")) return false;
  return true;
}

bool test_export_rejects_lossy_document_definitions() {
  auto path = round_trip_document();
  io::StyledPath styled{path, QStringLiteral("#ffffff"), QString(), 1.0};
  styled.path.subpaths.last().closed = false;
  QString error;
  if (!check(io::export_svg({styled}, QSizeF(120.0, 80.0), &error).isEmpty() &&
                 error.contains(QStringLiteral("export requires a closed contour")),
             "Do not export a filled open contour that SVG would implicitly close")) return false;
  styled.fill_color.clear();
  styled.stroke_color = QStringLiteral("#ffffff");
  const auto bytes = io::export_svg({styled}, QSizeF(120.0, 80.0), &error);
  if (!check(!bytes.isEmpty(), "Export open stroked paths without adding fill closure")) return false;
  return true;
}

bool test_normalized_export_uses_requested_viewbox_units() {
  io::StyledPath styled;
  styled.path.coordinate_space = paths::CoordinateSpace::Normalized;
  paths::VectorSubpath subpath;
  subpath.id = QStringLiteral("normalized-contour");
  subpath.closed = true;
  subpath.anchors = {
      anchor(QStringLiteral("a"), QPointF(0.1, 0.1)),
      anchor(QStringLiteral("b"), QPointF(0.9, 0.1)),
      anchor(QStringLiteral("c"), QPointF(0.5, 0.9))};
  styled.path.subpaths = {subpath};
  styled.fill_color = QStringLiteral("#ffffff");
  QString error;
  const auto bytes = io::export_svg({styled}, QSizeF(100.0, 50.0), &error);
  if (!check(!bytes.isEmpty(), "Export normalized authored paths using an explicit viewBox")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!check(bytes.contains("M 10 5") && bytes.contains("L 90 5") &&
                 bytes.contains("L 50 45"),
             "Scale normalized anchor coordinates into SVG viewBox units")) return false;
  QVector<io::StyledPath> reopened;
  if (!check(import_svg_document(bytes, &reopened, &error), "Reimport normalized-path export")) return false;
  const auto& first = reopened.first().path.subpaths.first().anchors.first().position;
  if (!check(close(first.x(), 10.0) && close(first.y(), 5.0),
             "Round-trip normalized paths in document coordinates")) return false;

  io::StyledPath document_path;
  document_path.path = round_trip_document();
  document_path.stroke_color = QStringLiteral("#ffffff");
  document_path.stroke_width = 6.0;
  const auto scaled_bytes = io::export_svg({document_path}, QSizeF(240.0, 160.0), &error);
  if (!check(!scaled_bytes.isEmpty(), "Scale document-space geometry into a different export viewBox")) return false;
  QVector<io::StyledPath> scaled_reopened;
  if (!check(import_svg_document(scaled_bytes, &scaled_reopened, &error),
             "Reimport document-space geometry exported at a new viewBox size")) return false;
  const auto& scaled_first = scaled_reopened.first().path.subpaths.first().anchors.first().position;
  if (!check(close(scaled_first.x(), 20.0) && close(scaled_first.y(), 20.0) &&
                 close(scaled_reopened.first().stroke_width, 6.0),
             "Scale document geometry while keeping caller-supplied frame-pixel stroke width")) return false;
  return true;
}

}  // namespace

int main() {
  if (!test_commands_viewbox_transforms_and_styles() ||
      !test_relative_smooth_and_alpha() ||
      !test_affine_matrix_and_stroke_scaling() ||
      !test_round_trip_preserves_cubics_and_styles() ||
      !test_nonzero_semantics_and_unsupported_features() ||
      !test_export_rejects_lossy_document_definitions() ||
      !test_normalized_export_uses_requested_viewbox_units()) {
    return 1;
  }
  std::cout << "photo_vector_path_io_test passed\n";
  return 0;
}
