#include "photo_vector_mask.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

#include <cmath>

namespace photo_editor::desktop::vector_masks {
namespace {

constexpr qsizetype kMaximumMaskIdLength = 128;
constexpr int kMaximumRasterDimension = 32768;
constexpr qint64 kMaximumRasterPixels = 64LL * 1024LL * 1024LL;

void set_error(QString* error, const QString& message) {
  if (error) *error = message;
}

bool fail(QString* error, const QString& message) {
  set_error(error, message);
  return false;
}

bool json_keys_are(const QJsonObject& object, const QStringList& keys) {
  if (object.size() != keys.size()) return false;
  for (const auto& key : keys)
    if (!object.contains(key)) return false;
  return true;
}

QString basis_name(CoordinateBasis basis) {
  switch (basis) {
    case CoordinateBasis::LocalLayer:
      return QStringLiteral("local-layer");
    case CoordinateBasis::SourceDocument:
      return QStringLiteral("source-document");
  }
  return {};
}

bool parse_basis(const QString& value, CoordinateBasis* basis) {
  if (value == QStringLiteral("local-layer")) {
    *basis = CoordinateBasis::LocalLayer;
    return true;
  }
  if (value == QStringLiteral("source-document")) {
    *basis = CoordinateBasis::SourceDocument;
    return true;
  }
  return false;
}

bool valid_raster_size(const QSize& size) {
  if (!size.isValid() || size.isEmpty() ||
      size.width() > kMaximumRasterDimension ||
      size.height() > kMaximumRasterDimension) {
    return false;
  }
  return static_cast<qint64>(size.width()) *
             static_cast<qint64>(size.height()) <=
         kMaximumRasterPixels;
}

}  // namespace

QString validate(const PhotoVectorMask& mask) {
  if (mask.version != kPhotoVectorMaskSchemaVersion)
    return QStringLiteral("Unsupported photo vector mask schema version.");
  if (mask.id.trimmed().isEmpty() || mask.id.size() > kMaximumMaskIdLength)
    return QStringLiteral("A vector mask needs a nonempty ID of at most 128 characters.");
  if (mask.basis != CoordinateBasis::LocalLayer &&
      mask.basis != CoordinateBasis::SourceDocument) {
    return QStringLiteral("Choose local-layer or source-document coordinates.");
  }
  if (!std::isfinite(mask.density) || mask.density < 0.0 ||
      mask.density > 1.0) {
    return QStringLiteral("Vector mask density must be finite and between 0 and 1.");
  }
  const auto path_error = vector_paths::validate(mask.path);
  if (!path_error.isEmpty()) return path_error;
  if (mask.path.subpaths.isEmpty())
    return QStringLiteral("A vector mask needs at least one closed contour.");
  for (const auto& subpath : mask.path.subpaths) {
    if (!subpath.closed || subpath.anchors.size() < 3)
      return QStringLiteral("Every vector mask contour must be closed and contain at least three anchors.");
  }
  return {};
}

QJsonObject to_json(const PhotoVectorMask& mask, QString* error) {
  const auto validation_error = validate(mask);
  if (!validation_error.isEmpty()) {
    set_error(error, validation_error);
    return {};
  }
  QString path_error;
  const auto path = vector_paths::to_json(mask.path, &path_error);
  if (!path_error.isEmpty()) {
    set_error(error, path_error);
    return {};
  }
  set_error(error, {});
  return QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("photo-vector-mask")},
      {QStringLiteral("version"), mask.version},
      {QStringLiteral("id"), mask.id},
      {QStringLiteral("basis"), basis_name(mask.basis)},
      {QStringLiteral("enabled"), mask.enabled},
      {QStringLiteral("invert"), mask.inverted},
      {QStringLiteral("density"), mask.density},
      {QStringLiteral("path"), path}};
}

bool from_json(const QJsonObject& object, PhotoVectorMask* mask, QString* error) {
  if (!mask) return fail(error, QStringLiteral("A vector mask output is required."));
  if (!json_keys_are(object,
                     {QStringLiteral("schema"), QStringLiteral("version"),
                      QStringLiteral("id"), QStringLiteral("basis"),
                      QStringLiteral("enabled"), QStringLiteral("invert"),
                      QStringLiteral("density"), QStringLiteral("path")}) ||
      object.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("photo-vector-mask") ||
      !object.value(QStringLiteral("version")).isDouble() ||
      object.value(QStringLiteral("version")).toInt(-1) !=
          kPhotoVectorMaskSchemaVersion ||
      !object.value(QStringLiteral("id")).isString() ||
      !object.value(QStringLiteral("basis")).isString() ||
      !object.value(QStringLiteral("enabled")).isBool() ||
      !object.value(QStringLiteral("invert")).isBool() ||
      !object.value(QStringLiteral("density")).isDouble() ||
      !object.value(QStringLiteral("path")).isObject()) {
    return fail(error, QStringLiteral("Unsupported or malformed vector mask JSON."));
  }

  PhotoVectorMask parsed;
  parsed.version = object.value(QStringLiteral("version")).toInt();
  parsed.id = object.value(QStringLiteral("id")).toString();
  if (!parse_basis(object.value(QStringLiteral("basis")).toString(),
                   &parsed.basis)) {
    return fail(error, QStringLiteral("Vector mask basis must be local-layer or source-document."));
  }
  parsed.enabled = object.value(QStringLiteral("enabled")).toBool();
  parsed.inverted = object.value(QStringLiteral("invert")).toBool();
  parsed.density = object.value(QStringLiteral("density")).toDouble();
  if (!vector_paths::from_json(object.value(QStringLiteral("path")).toObject(),
                               &parsed.path, error)) {
    return false;
  }
  const auto validation_error = validate(parsed);
  if (!validation_error.isEmpty()) return fail(error, validation_error);
  *mask = std::move(parsed);
  set_error(error, {});
  return true;
}

QVariantMap to_variant(const PhotoVectorMask& mask, QString* error) {
  return to_json(mask, error).toVariantMap();
}

bool from_variant(const QVariantMap& value, PhotoVectorMask* mask,
                  QString* error) {
  return from_json(QJsonObject::fromVariantMap(value), mask, error);
}

QByteArray serialize(const PhotoVectorMask& mask, QString* error) {
  const auto object = to_json(mask, error);
  if (object.isEmpty()) return {};
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool deserialize(const QByteArray& bytes, PhotoVectorMask* mask,
                 QString* error) {
  if (!mask) return fail(error, QStringLiteral("A vector mask output is required."));
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    return fail(error, QStringLiteral("Vector mask recipe is not a valid JSON object."));
  return from_json(document.object(), mask, error);
}

QImage rasterize_mask(const PhotoVectorMask& mask, const QSize& basis_size,
                      QString* error) {
  const auto validation_error = validate(mask);
  if (!validation_error.isEmpty()) {
    set_error(error, validation_error);
    return {};
  }
  if (!valid_raster_size(basis_size)) {
    set_error(error, QStringLiteral("Mask dimensions must be positive, at most 32768 per side, and at most 64 megapixels."));
    return {};
  }

  QImage result;
  if (mask.enabled) {
    result = vector_paths::rasterize_mask(mask.path, basis_size, error);
    if (result.isNull()) return {};
  } else {
    result = QImage(basis_size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) {
      set_error(error, QStringLiteral("The requested vector mask raster could not be allocated."));
      return {};
    }
    result.fill(Qt::white);
    set_error(error, {});
    return result;
  }

  for (int y = 0; y < result.height(); ++y) {
    auto* pixels = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      const int source_alpha = qAlpha(pixels[x]);
      const int oriented_alpha = mask.inverted ? 255 - source_alpha : source_alpha;
      const int coverage = 255 - static_cast<int>(std::lround(
          static_cast<double>(255 - oriented_alpha) * mask.density));
      pixels[x] = qRgba(coverage, coverage, coverage, coverage);
    }
  }
  set_error(error, {});
  return result;
}

}  // namespace photo_editor::desktop::vector_masks
