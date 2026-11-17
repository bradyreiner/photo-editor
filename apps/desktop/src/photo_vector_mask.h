#pragma once

#include "photo_vector_path.h"

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop::vector_masks {

inline constexpr int kPhotoVectorMaskSchemaVersion = 1;

// The path is interpreted in a coordinate frame selected by the owning layer.
// LocalLayer follows that layer's untransformed pixel bounds; SourceDocument
// follows the original source image bounds. The renderer maps that frame through
// the layer placement before combining the resulting coverage.
enum class CoordinateBasis {
  LocalLayer,
  SourceDocument
};

struct PhotoVectorMask {
  int version = kPhotoVectorMaskSchemaVersion;
  QString id;
  CoordinateBasis basis = CoordinateBasis::LocalLayer;
  bool enabled = true;
  bool inverted = false;
  double density = 1.0;
  vector_paths::VectorPathDocument path;
};

// Returns a user-readable validation error, or an empty string for valid data.
// Vector masks accept only nonempty collections of closed cubic-Bezier contours.
QString validate(const PhotoVectorMask& mask);

QJsonObject to_json(const PhotoVectorMask& mask, QString* error = nullptr);
bool from_json(const QJsonObject& object, PhotoVectorMask* mask,
               QString* error = nullptr);
QVariantMap to_variant(const PhotoVectorMask& mask, QString* error = nullptr);
bool from_variant(const QVariantMap& value, PhotoVectorMask* mask,
                  QString* error = nullptr);
QByteArray serialize(const PhotoVectorMask& mask, QString* error = nullptr);
bool deserialize(const QByteArray& bytes, PhotoVectorMask* mask,
                 QString* error = nullptr);

// Produces antialiased white RGBA coverage at basis_size. Density moves the
// oriented mask toward full coverage: output = 1 - density * (1 - alpha).
// The caller supplies
// dimensions in the declared coordinate basis and maps/composes that coverage
// with the layer's existing raster or selection alpha. No source pixels change.
// Disabled masks return full coverage after validation, so bypass never hides a
// malformed or unsupported recipe.
QImage rasterize_mask(const PhotoVectorMask& mask, const QSize& basis_size,
                      QString* error = nullptr);

}  // namespace photo_editor::desktop::vector_masks
