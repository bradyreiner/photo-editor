#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop::vector_paths {

inline constexpr int kVectorPathSchemaVersion = 1;

enum class CoordinateSpace {
  Normalized,
  Document
};

enum class AnchorKind {
  Corner,
  Smooth
};

enum class HandleSide {
  Incoming,
  Outgoing
};

enum class BooleanOperation {
  Union,
  Subtract,
  Intersect,
  Exclude
};

// Handle coordinates are offsets from the anchor. This keeps a handle attached
// when its anchor moves and makes linked handles straightforward to edit.
struct PathAnchor {
  QString id;
  QPointF position;
  QPointF incoming;
  QPointF outgoing;
  bool linked_handles = false;
  AnchorKind kind = AnchorKind::Corner;
};

struct VectorSubpath {
  QString id;
  bool closed = false;
  QVector<PathAnchor> anchors;
};

struct VectorPathDocument {
  int version = kVectorPathSchemaVersion;
  CoordinateSpace coordinate_space = CoordinateSpace::Normalized;
  QSizeF document_size = QSizeF(0.0, 0.0);
  QVector<VectorSubpath> subpaths;
};

// The model is deliberately bounded so imported or malformed recipes cannot
// create unbounded geometry or raster allocations.
QString validate(const VectorPathDocument& document);

QJsonObject to_json(const VectorPathDocument& document, QString* error = nullptr);
bool from_json(const QJsonObject& object, VectorPathDocument* document,
               QString* error = nullptr);
QVariantMap to_variant(const VectorPathDocument& document, QString* error = nullptr);
bool to_document(const QVariantMap& value, VectorPathDocument* document,
                 QString* error = nullptr);
QByteArray serialize(const VectorPathDocument& document, QString* error = nullptr);
bool deserialize(const QByteArray& bytes, VectorPathDocument* document,
                 QString* error = nullptr);

bool add_subpath(VectorPathDocument* document, const QString& id, bool closed = false,
                 QString* error = nullptr);
bool append_anchor(VectorPathDocument* document, const QString& subpath_id,
                   const PathAnchor& anchor, QString* error = nullptr);
bool remove_anchor(VectorPathDocument* document, const QString& subpath_id,
                   const QString& anchor_id, QString* error = nullptr);
bool move_anchor(VectorPathDocument* document, const QString& subpath_id,
                 const QString& anchor_id, const QPointF& position,
                 QString* error = nullptr);
bool set_handle(VectorPathDocument* document, const QString& subpath_id,
                const QString& anchor_id, HandleSide side, const QPointF& offset,
                QString* error = nullptr);
bool set_anchor_kind(VectorPathDocument* document, const QString& subpath_id,
                     const QString& anchor_id, AnchorKind kind,
                     QString* error = nullptr);
bool set_closed(VectorPathDocument* document, const QString& subpath_id, bool closed,
                QString* error = nullptr);

// Coordinates are scaled to target_size. Normalized paths use the target
// dimensions directly; document paths scale from their declared document_size.
QPainterPath to_painter_path(const VectorPathDocument& document,
                             const QSize& target_size, QString* error = nullptr);

// Boolean results are display/raster geometry. Both source documents remain
// untouched and fully editable for the caller to keep as operands.
QPainterPath combine_paths(const VectorPathDocument& lhs,
                           const VectorPathDocument& rhs,
                           BooleanOperation operation,
                           const QSize& target_size,
                           QString* error = nullptr);

QImage rasterize_fill(const VectorPathDocument& document, const QSize& size,
                      const QColor& color = Qt::white, QString* error = nullptr);
QImage rasterize_stroke(const VectorPathDocument& document, const QSize& size,
                        const QColor& color, double width_pixels,
                        QString* error = nullptr);
QImage rasterize_mask(const VectorPathDocument& document, const QSize& size,
                      QString* error = nullptr);

}  // namespace photo_editor::desktop::vector_paths
