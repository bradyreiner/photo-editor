#pragma once

#include "photo_vector_path.h"

#include <QByteArray>
#include <QSizeF>
#include <QString>
#include <QVector>

namespace photo_editor::desktop::vector_path_io {

// A deliberately small SVG interchange record. The path remains an editable
// cubic document; style is limited to a solid fill, solid stroke and width.
struct StyledPath {
  vector_paths::VectorPathDocument path;
  QString fill_color;    // Empty means SVG fill="none"; otherwise QColor hex/name.
  QString stroke_color; // Empty means SVG stroke="none"; otherwise QColor hex/name.
  double stroke_width = 1.0;
};

// Supported input: one bounded SVG document with a viewBox, nested <g>
// transforms/styles, and one or more <path> elements. Path commands M/L/H/V/C/S/Q/T/Z
// (absolute or relative) are converted to editable cubic anchors. Unsupported
// SVG constructs are rejected with a user-facing explanation.
bool import_svg(const QByteArray& bytes, QVector<StyledPath>* paths,
                QString* error = nullptr);

// Writes a deterministic SVG path-only document, scaling editable path
// coordinates into the requested viewBox. Stroke width is supplied in output
// viewBox units. The only emitted fill rule is evenodd, matching the
// vector-path rasterizer's supported semantics.
QByteArray export_svg(const QVector<StyledPath>& paths, const QSizeF& view_box,
                      QString* error = nullptr);

}  // namespace photo_editor::desktop::vector_path_io
