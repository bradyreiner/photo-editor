#include "photo_vector_path.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>
#include <QPen>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace photo_editor::desktop::vector_paths {
namespace {

constexpr qsizetype kMaxSubpaths = 256;
constexpr qsizetype kMaxAnchors = 8192;
constexpr qsizetype kMaxIdLength = 128;
constexpr int kMaxRasterDimension = 32768;
constexpr qint64 kMaxRasterPixels = 64LL * 1024LL * 1024LL;

bool finite(double value) {
  return std::isfinite(value);
}

bool finite(const QPointF& point) {
  return finite(point.x()) && finite(point.y());
}

bool finite(const QSizeF& size) {
  return finite(size.width()) && finite(size.height());
}

void set_error(QString* error, const QString& message) {
  if (error) *error = message;
}

bool fail(QString* error, const QString& message) {
  set_error(error, message);
  return false;
}

bool valid_id(const QString& id) {
  return !id.trimmed().isEmpty() && id.size() <= kMaxIdLength;
}

bool json_keys_are(const QJsonObject& object, const QStringList& keys) {
  if (object.size() != keys.size()) return false;
  for (const auto& key : keys)
    if (!object.contains(key)) return false;
  return true;
}

QJsonArray point_json(const QPointF& point) {
  return {point.x(), point.y()};
}

QJsonArray size_json(const QSizeF& size) {
  return {size.width(), size.height()};
}

bool parse_point(const QJsonValue& value, QPointF* point) {
  if (!value.isArray() || value.toArray().size() != 2) return false;
  const auto array = value.toArray();
  if (!array.at(0).isDouble() || !array.at(1).isDouble()) return false;
  const QPointF parsed(array.at(0).toDouble(), array.at(1).toDouble());
  if (!finite(parsed)) return false;
  *point = parsed;
  return true;
}

QString coordinate_space_name(CoordinateSpace space) {
  return space == CoordinateSpace::Normalized ? QStringLiteral("normalized")
                                               : QStringLiteral("document");
}

QString anchor_kind_name(AnchorKind kind) {
  return kind == AnchorKind::Smooth ? QStringLiteral("smooth")
                                     : QStringLiteral("corner");
}

bool parse_anchor_kind(const QString& name, AnchorKind* kind) {
  if (name == QStringLiteral("corner")) {
    *kind = AnchorKind::Corner;
    return true;
  }
  if (name == QStringLiteral("smooth")) {
    *kind = AnchorKind::Smooth;
    return true;
  }
  return false;
}

qsizetype anchor_count(const VectorPathDocument& document) {
  qsizetype total = 0;
  for (const auto& subpath : document.subpaths) total += subpath.anchors.size();
  return total;
}

qsizetype find_subpath(const VectorPathDocument& document, const QString& id) {
  for (qsizetype i = 0; i < document.subpaths.size(); ++i)
    if (document.subpaths.at(i).id == id) return i;
  return -1;
}

qsizetype find_anchor(const VectorSubpath& subpath, const QString& id) {
  for (qsizetype i = 0; i < subpath.anchors.size(); ++i)
    if (subpath.anchors.at(i).id == id) return i;
  return -1;
}

bool valid_raster_size(const QSize& size) {
  if (!size.isValid() || size.isEmpty() || size.width() > kMaxRasterDimension ||
      size.height() > kMaxRasterDimension)
    return false;
  return static_cast<qint64>(size.width()) * static_cast<qint64>(size.height()) <=
         kMaxRasterPixels;
}

QPointF map_point(const QPointF& point, const VectorPathDocument& document,
                  const QSize& target_size) {
  const double width_scale = document.coordinate_space == CoordinateSpace::Normalized
      ? static_cast<double>(target_size.width())
      : static_cast<double>(target_size.width()) / document.document_size.width();
  const double height_scale = document.coordinate_space == CoordinateSpace::Normalized
      ? static_cast<double>(target_size.height())
      : static_cast<double>(target_size.height()) / document.document_size.height();
  return QPointF(point.x() * width_scale, point.y() * height_scale);
}

QPainterPath painter_path(const VectorPathDocument& document, const QSize& target_size,
                          bool closed_only) {
  QPainterPath result;
  result.setFillRule(Qt::OddEvenFill);
  for (const auto& subpath : document.subpaths) {
    if (closed_only && !subpath.closed) continue;
    if (subpath.anchors.isEmpty()) continue;
    const auto& first = subpath.anchors.first();
    result.moveTo(map_point(first.position, document, target_size));
    const qsizetype segment_count = subpath.closed ? subpath.anchors.size()
                                                   : subpath.anchors.size() - 1;
    for (qsizetype i = 0; i < segment_count; ++i) {
      const auto& start = subpath.anchors.at(i);
      const auto& end = subpath.anchors.at((i + 1) % subpath.anchors.size());
      if (start.outgoing.isNull() && end.incoming.isNull()) {
        result.lineTo(map_point(end.position, document, target_size));
      } else {
        result.cubicTo(
            map_point(start.position + start.outgoing, document, target_size),
            map_point(end.position + end.incoming, document, target_size),
            map_point(end.position, document, target_size));
      }
    }
    if (subpath.closed) result.closeSubpath();
  }
  return result;
}

bool mutate(VectorPathDocument* document,
            const std::function<bool(VectorPathDocument&, QString*)>& operation,
            QString* error) {
  if (!document) return fail(error, QStringLiteral("A path document is required."));
  const auto initial_error = validate(*document);
  if (!initial_error.isEmpty()) return fail(error, initial_error);
  auto candidate = *document;
  if (!operation(candidate, error)) return false;
  const auto candidate_error = validate(candidate);
  if (!candidate_error.isEmpty()) return fail(error, candidate_error);
  *document = std::move(candidate);
  set_error(error, {});
  return true;
}

QImage rasterize(const VectorPathDocument& document, const QSize& size,
                 const QColor& color, bool stroke, double width_pixels,
                 QString* error) {
  const auto document_error = validate(document);
  if (!document_error.isEmpty()) {
    set_error(error, document_error);
    return {};
  }
  if (!valid_raster_size(size)) {
    set_error(error, QStringLiteral("Raster dimensions must be positive, at most 32768 per side, and at most 64 megapixels."));
    return {};
  }
  if (!color.isValid()) {
    set_error(error, QStringLiteral("Choose a valid raster color."));
    return {};
  }
  if (stroke && (!finite(width_pixels) || width_pixels <= 0.0 || width_pixels > 4096.0)) {
    set_error(error, QStringLiteral("Stroke width must be finite and between 0 and 4096 pixels."));
    return {};
  }
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    set_error(error, QStringLiteral("The requested path raster could not be allocated."));
    return {};
  }
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  if (stroke) {
    painter.setPen(QPen(color, width_pixels, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(painter_path(document, size, false));
  } else {
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(painter_path(document, size, true));
  }
  painter.end();
  set_error(error, {});
  return image;
}

}  // namespace

QString validate(const VectorPathDocument& document) {
  if (document.version != kVectorPathSchemaVersion)
    return QStringLiteral("Unsupported vector path schema version.");
  if (document.subpaths.size() > kMaxSubpaths)
    return QStringLiteral("A vector path can contain at most 256 subpaths.");
  if (document.coordinate_space == CoordinateSpace::Normalized) {
    if (document.document_size.width() != 0.0 || document.document_size.height() != 0.0)
      return QStringLiteral("Normalized paths cannot declare a document size.");
  } else if (document.coordinate_space == CoordinateSpace::Document) {
    if (!finite(document.document_size) || document.document_size.width() <= 0.0 ||
        document.document_size.height() <= 0.0 ||
        document.document_size.width() > 32768.0 || document.document_size.height() > 32768.0)
      return QStringLiteral("Document paths need finite positive dimensions up to 32768.");
  } else {
    return QStringLiteral("Choose normalized or document coordinates.");
  }

  qsizetype total_anchors = 0;
  QSet<QString> ids;
  for (const auto& subpath : document.subpaths) {
    if (!valid_id(subpath.id) || ids.contains(subpath.id))
      return QStringLiteral("Subpath IDs must be nonempty, bounded, and unique.");
    ids.insert(subpath.id);
    if (subpath.anchors.size() > kMaxAnchors)
      return QStringLiteral("A subpath can contain at most 8192 anchors.");
    if (subpath.closed && subpath.anchors.size() < 3)
      return QStringLiteral("A closed subpath needs at least three anchors.");
    total_anchors += subpath.anchors.size();
    if (total_anchors > kMaxAnchors)
      return QStringLiteral("A vector path can contain at most 8192 anchors.");
    for (const auto& anchor : subpath.anchors) {
      if (!valid_id(anchor.id) || ids.contains(anchor.id))
        return QStringLiteral("Anchor and subpath IDs must be nonempty, bounded, and unique.");
      ids.insert(anchor.id);
      if (!finite(anchor.position) || !finite(anchor.incoming) || !finite(anchor.outgoing))
        return QStringLiteral("Path coordinates and handles must be finite.");
      const double x_max = document.coordinate_space == CoordinateSpace::Normalized
          ? 1.0 : document.document_size.width();
      const double y_max = document.coordinate_space == CoordinateSpace::Normalized
          ? 1.0 : document.document_size.height();
      if (anchor.position.x() < 0.0 || anchor.position.x() > x_max ||
          anchor.position.y() < 0.0 || anchor.position.y() > y_max)
        return QStringLiteral("Anchor positions must stay inside their coordinate space.");
      const double x_handle_limit = document.coordinate_space == CoordinateSpace::Normalized
          ? 4.0 : std::max(4.0, document.document_size.width() * 4.0);
      const double y_handle_limit = document.coordinate_space == CoordinateSpace::Normalized
          ? 4.0 : std::max(4.0, document.document_size.height() * 4.0);
      if (std::abs(anchor.incoming.x()) > x_handle_limit ||
          std::abs(anchor.outgoing.x()) > x_handle_limit ||
          std::abs(anchor.incoming.y()) > y_handle_limit ||
          std::abs(anchor.outgoing.y()) > y_handle_limit)
        return QStringLiteral("Bezier handles exceed the bounded coordinate range.");
      if (anchor.kind != AnchorKind::Corner && anchor.kind != AnchorKind::Smooth)
        return QStringLiteral("Choose corner or smooth anchor handling.");
      if (anchor.linked_handles && anchor.kind != AnchorKind::Smooth)
        return QStringLiteral("Only smooth anchors can have linked handles.");
      if (anchor.linked_handles) {
        const double cross = anchor.incoming.x() * anchor.outgoing.y() -
                             anchor.incoming.y() * anchor.outgoing.x();
        const double dot = anchor.incoming.x() * anchor.outgoing.x() +
                           anchor.incoming.y() * anchor.outgoing.y();
        const double scale = std::hypot(anchor.incoming.x(), anchor.incoming.y()) *
                             std::hypot(anchor.outgoing.x(), anchor.outgoing.y());
        if ((scale == 0.0 && (!anchor.incoming.isNull() || !anchor.outgoing.isNull())) ||
            (scale > 0.0 && (std::abs(cross) > scale * 1e-9 || dot > 0.0)))
          return QStringLiteral("Linked smooth handles must point in opposite directions.");
      }
    }
  }
  return {};
}

QJsonObject to_json(const VectorPathDocument& document, QString* error) {
  const auto validation_error = validate(document);
  if (!validation_error.isEmpty()) {
    set_error(error, validation_error);
    return {};
  }
  QJsonArray subpaths;
  for (const auto& subpath : document.subpaths) {
    QJsonArray anchors;
    for (const auto& anchor : subpath.anchors) {
      anchors.append(QJsonObject{
          {QStringLiteral("id"), anchor.id},
          {QStringLiteral("point"), point_json(anchor.position)},
          {QStringLiteral("in"), point_json(anchor.incoming)},
          {QStringLiteral("out"), point_json(anchor.outgoing)},
          {QStringLiteral("handlesLinked"), anchor.linked_handles},
          {QStringLiteral("kind"), anchor_kind_name(anchor.kind)}});
    }
    subpaths.append(QJsonObject{
        {QStringLiteral("id"), subpath.id},
        {QStringLiteral("closed"), subpath.closed},
        {QStringLiteral("anchors"), anchors}});
  }
  QJsonObject object{
      {QStringLiteral("schema"), QStringLiteral("photo-vector-path")},
      {QStringLiteral("version"), document.version},
      {QStringLiteral("coordinateSpace"), coordinate_space_name(document.coordinate_space)},
      {QStringLiteral("subpaths"), subpaths}};
  if (document.coordinate_space == CoordinateSpace::Document)
    object.insert(QStringLiteral("documentSize"), size_json(document.document_size));
  set_error(error, {});
  return object;
}

bool from_json(const QJsonObject& object, VectorPathDocument* document, QString* error) {
  if (!document) return fail(error, QStringLiteral("A path document output is required."));
  const auto space = object.value(QStringLiteral("coordinateSpace")).toString();
  const bool normalized = space == QStringLiteral("normalized");
  const bool document_space = space == QStringLiteral("document");
  if ((!normalized && !document_space) ||
      !json_keys_are(object, normalized
          ? QStringList{QStringLiteral("schema"), QStringLiteral("version"),
                        QStringLiteral("coordinateSpace"), QStringLiteral("subpaths")}
          : QStringList{QStringLiteral("schema"), QStringLiteral("version"),
                        QStringLiteral("coordinateSpace"), QStringLiteral("documentSize"),
                        QStringLiteral("subpaths")}))
    return fail(error, QStringLiteral("Path JSON has missing, extra, or invalid top-level fields."));
  if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("photo-vector-path") ||
      !object.value(QStringLiteral("version")).isDouble() ||
      object.value(QStringLiteral("version")).toInt(-1) != kVectorPathSchemaVersion ||
      !object.value(QStringLiteral("subpaths")).isArray())
    return fail(error, QStringLiteral("Unsupported or malformed vector path JSON."));

  VectorPathDocument parsed;
  parsed.coordinate_space = normalized ? CoordinateSpace::Normalized : CoordinateSpace::Document;
  parsed.version = object.value(QStringLiteral("version")).toInt();
  if (document_space) {
    const auto value = object.value(QStringLiteral("documentSize"));
    if (!value.isArray() || value.toArray().size() != 2 ||
        !value.toArray().at(0).isDouble() || !value.toArray().at(1).isDouble())
      return fail(error, QStringLiteral("Document size must contain two numeric values."));
    parsed.document_size = QSizeF(value.toArray().at(0).toDouble(),
                                  value.toArray().at(1).toDouble());
  }

  const auto subpaths = object.value(QStringLiteral("subpaths")).toArray();
  if (subpaths.size() > kMaxSubpaths)
    return fail(error, QStringLiteral("A vector path can contain at most 256 subpaths."));
  parsed.subpaths.reserve(subpaths.size());
  for (const auto& subpath_value : subpaths) {
    if (!subpath_value.isObject())
      return fail(error, QStringLiteral("Each subpath must be an object."));
    const auto subpath_object = subpath_value.toObject();
    if (!json_keys_are(subpath_object, {QStringLiteral("id"), QStringLiteral("closed"),
                                        QStringLiteral("anchors")}) ||
        !subpath_object.value(QStringLiteral("id")).isString() ||
        !subpath_object.value(QStringLiteral("closed")).isBool() ||
        !subpath_object.value(QStringLiteral("anchors")).isArray())
      return fail(error, QStringLiteral("Subpath JSON has missing, extra, or invalid fields."));
    VectorSubpath subpath;
    subpath.id = subpath_object.value(QStringLiteral("id")).toString();
    subpath.closed = subpath_object.value(QStringLiteral("closed")).toBool();
    const auto anchors = subpath_object.value(QStringLiteral("anchors")).toArray();
    if (anchors.size() > kMaxAnchors)
      return fail(error, QStringLiteral("A subpath can contain at most 8192 anchors."));
    subpath.anchors.reserve(anchors.size());
    for (const auto& anchor_value : anchors) {
      if (!anchor_value.isObject())
        return fail(error, QStringLiteral("Each anchor must be an object."));
      const auto anchor_object = anchor_value.toObject();
      if (!json_keys_are(anchor_object, {QStringLiteral("id"), QStringLiteral("point"),
                                         QStringLiteral("in"), QStringLiteral("out"),
                                         QStringLiteral("handlesLinked"), QStringLiteral("kind")}) ||
          !anchor_object.value(QStringLiteral("id")).isString() ||
          !anchor_object.value(QStringLiteral("handlesLinked")).isBool() ||
          !anchor_object.value(QStringLiteral("kind")).isString())
        return fail(error, QStringLiteral("Anchor JSON has missing, extra, or invalid fields."));
      PathAnchor anchor;
      anchor.id = anchor_object.value(QStringLiteral("id")).toString();
      anchor.linked_handles = anchor_object.value(QStringLiteral("handlesLinked")).toBool();
      if (!parse_point(anchor_object.value(QStringLiteral("point")), &anchor.position) ||
          !parse_point(anchor_object.value(QStringLiteral("in")), &anchor.incoming) ||
          !parse_point(anchor_object.value(QStringLiteral("out")), &anchor.outgoing) ||
          !parse_anchor_kind(anchor_object.value(QStringLiteral("kind")).toString(), &anchor.kind))
        return fail(error, QStringLiteral("Anchor coordinates or kind are malformed."));
      subpath.anchors.append(std::move(anchor));
    }
    parsed.subpaths.append(std::move(subpath));
  }
  const auto validation_error = validate(parsed);
  if (!validation_error.isEmpty()) return fail(error, validation_error);
  *document = std::move(parsed);
  set_error(error, {});
  return true;
}

QVariantMap to_variant(const VectorPathDocument& document, QString* error) {
  return to_json(document, error).toVariantMap();
}

bool to_document(const QVariantMap& value, VectorPathDocument* document, QString* error) {
  return from_json(QJsonObject::fromVariantMap(value), document, error);
}

QByteArray serialize(const VectorPathDocument& document, QString* error) {
  const auto object = to_json(document, error);
  if (object.isEmpty()) return {};
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool deserialize(const QByteArray& bytes, VectorPathDocument* document, QString* error) {
  QJsonParseError parse_error;
  const auto json = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !json.isObject())
    return fail(error, QStringLiteral("Path recipe bytes are not a JSON object."));
  return from_json(json.object(), document, error);
}

bool add_subpath(VectorPathDocument* document, const QString& id, bool closed,
                 QString* error) {
  return mutate(document, [&id, closed](VectorPathDocument& candidate, QString* inner_error) {
    if (closed)
      return fail(inner_error, QStringLiteral("Create an open subpath, add three anchors, then close it."));
    if (!valid_id(id)) return fail(inner_error, QStringLiteral("Subpath ID is empty or too long."));
    if (candidate.subpaths.size() >= kMaxSubpaths)
      return fail(inner_error, QStringLiteral("A vector path can contain at most 256 subpaths."));
    for (const auto& subpath : candidate.subpaths) {
      if (subpath.id == id || find_anchor(subpath, id) >= 0)
        return fail(inner_error, QStringLiteral("Path IDs must be unique."));
    }
    candidate.subpaths.append(VectorSubpath{id, closed, {}});
    return true;
  }, error);
}

bool append_anchor(VectorPathDocument* document, const QString& subpath_id,
                   const PathAnchor& anchor, QString* error) {
  return mutate(document, [&subpath_id, &anchor](VectorPathDocument& candidate, QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    if (anchor_count(candidate) >= kMaxAnchors)
      return fail(inner_error, QStringLiteral("A vector path can contain at most 8192 anchors."));
    if (!valid_id(anchor.id)) return fail(inner_error, QStringLiteral("Anchor ID is empty or too long."));
    for (const auto& subpath : candidate.subpaths)
      if (subpath.id == anchor.id || find_anchor(subpath, anchor.id) >= 0)
        return fail(inner_error, QStringLiteral("Path IDs must be unique."));
    candidate.subpaths[index].anchors.append(anchor);
    return true;
  }, error);
}

bool remove_anchor(VectorPathDocument* document, const QString& subpath_id,
                   const QString& anchor_id, QString* error) {
  return mutate(document, [&subpath_id, &anchor_id](VectorPathDocument& candidate, QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    const auto anchor_index = find_anchor(candidate.subpaths[index], anchor_id);
    if (anchor_index < 0) return fail(inner_error, QStringLiteral("The requested anchor does not exist."));
    candidate.subpaths[index].anchors.removeAt(anchor_index);
    return true;
  }, error);
}

bool move_anchor(VectorPathDocument* document, const QString& subpath_id,
                 const QString& anchor_id, const QPointF& position, QString* error) {
  return mutate(document, [&subpath_id, &anchor_id, &position](VectorPathDocument& candidate,
                                                               QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    const auto anchor_index = find_anchor(candidate.subpaths[index], anchor_id);
    if (anchor_index < 0) return fail(inner_error, QStringLiteral("The requested anchor does not exist."));
    candidate.subpaths[index].anchors[anchor_index].position = position;
    return true;
  }, error);
}

bool set_handle(VectorPathDocument* document, const QString& subpath_id,
                const QString& anchor_id, HandleSide side, const QPointF& offset,
                QString* error) {
  return mutate(document, [&subpath_id, &anchor_id, side, &offset](VectorPathDocument& candidate,
                                                                  QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    const auto anchor_index = find_anchor(candidate.subpaths[index], anchor_id);
    if (anchor_index < 0) return fail(inner_error, QStringLiteral("The requested anchor does not exist."));
    auto& anchor = candidate.subpaths[index].anchors[anchor_index];
    if (side == HandleSide::Incoming) {
      anchor.incoming = offset;
      if (anchor.linked_handles) anchor.outgoing = -offset;
    } else if (side == HandleSide::Outgoing) {
      anchor.outgoing = offset;
      if (anchor.linked_handles) anchor.incoming = -offset;
    } else {
      return fail(inner_error, QStringLiteral("Choose an incoming or outgoing handle."));
    }
    return true;
  }, error);
}

bool set_anchor_kind(VectorPathDocument* document, const QString& subpath_id,
                     const QString& anchor_id, AnchorKind kind, QString* error) {
  return mutate(document, [&subpath_id, &anchor_id, kind](VectorPathDocument& candidate,
                                                          QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    const auto anchor_index = find_anchor(candidate.subpaths[index], anchor_id);
    if (anchor_index < 0) return fail(inner_error, QStringLiteral("The requested anchor does not exist."));
    if (kind != AnchorKind::Corner && kind != AnchorKind::Smooth)
      return fail(inner_error, QStringLiteral("Choose corner or smooth anchor handling."));
    auto& anchor = candidate.subpaths[index].anchors[anchor_index];
    anchor.kind = kind;
    anchor.linked_handles = kind == AnchorKind::Smooth;
    if (anchor.linked_handles && !anchor.incoming.isNull() && !anchor.outgoing.isNull()) {
      const double in_length = std::hypot(anchor.incoming.x(), anchor.incoming.y());
      const double out_length = std::hypot(anchor.outgoing.x(), anchor.outgoing.y());
      if (out_length > 0.0) {
        const QPointF direction = anchor.outgoing / out_length;
        anchor.incoming = -direction * in_length;
      } else {
        const double length = std::hypot(anchor.incoming.x(), anchor.incoming.y());
        if (length > 0.0) anchor.outgoing = -anchor.incoming;
      }
    } else if (anchor.linked_handles && !anchor.incoming.isNull()) {
      anchor.outgoing = -anchor.incoming;
    } else if (anchor.linked_handles && !anchor.outgoing.isNull()) {
      anchor.incoming = -anchor.outgoing;
    }
    return true;
  }, error);
}

bool set_closed(VectorPathDocument* document, const QString& subpath_id, bool closed,
                QString* error) {
  return mutate(document, [&subpath_id, closed](VectorPathDocument& candidate, QString* inner_error) {
    const auto index = find_subpath(candidate, subpath_id);
    if (index < 0) return fail(inner_error, QStringLiteral("The requested subpath does not exist."));
    candidate.subpaths[index].closed = closed;
    return true;
  }, error);
}

QPainterPath to_painter_path(const VectorPathDocument& document, const QSize& target_size,
                             QString* error) {
  const auto validation_error = validate(document);
  if (!validation_error.isEmpty()) {
    set_error(error, validation_error);
    return {};
  }
  if (!valid_raster_size(target_size)) {
    set_error(error, QStringLiteral("Target dimensions must be positive, at most 32768 per side, and at most 64 megapixels."));
    return {};
  }
  set_error(error, {});
  return painter_path(document, target_size, false);
}

QPainterPath combine_paths(const VectorPathDocument& lhs, const VectorPathDocument& rhs,
                           BooleanOperation operation, const QSize& target_size,
                           QString* error) {
  const auto left_error = validate(lhs);
  if (!left_error.isEmpty()) {
    set_error(error, left_error);
    return {};
  }
  const auto right_error = validate(rhs);
  if (!right_error.isEmpty()) {
    set_error(error, right_error);
    return {};
  }
  if (!valid_raster_size(target_size)) {
    set_error(error, QStringLiteral("Target dimensions must be positive, at most 32768 per side, and at most 64 megapixels."));
    return {};
  }
  const auto left = painter_path(lhs, target_size, true);
  const auto right = painter_path(rhs, target_size, true);
  set_error(error, {});
  switch (operation) {
    case BooleanOperation::Union: return left.united(right);
    case BooleanOperation::Subtract: return left.subtracted(right);
    case BooleanOperation::Intersect: return left.intersected(right);
    case BooleanOperation::Exclude:
      return left.subtracted(right).united(right.subtracted(left));
  }
  set_error(error, QStringLiteral("Choose a supported boolean path operation."));
  return {};
}

QImage rasterize_fill(const VectorPathDocument& document, const QSize& size,
                      const QColor& color, QString* error) {
  return rasterize(document, size, color, false, 0.0, error);
}

QImage rasterize_stroke(const VectorPathDocument& document, const QSize& size,
                        const QColor& color, double width_pixels, QString* error) {
  return rasterize(document, size, color, true, width_pixels, error);
}

QImage rasterize_mask(const VectorPathDocument& document, const QSize& size,
                      QString* error) {
  return rasterize(document, size, Qt::white, false, 0.0, error);
}

}  // namespace photo_editor::desktop::vector_paths
