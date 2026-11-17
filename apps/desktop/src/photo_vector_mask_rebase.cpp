#include "photo_vector_mask_rebase.h"

#include <cmath>
#include <utility>

namespace photo_editor::desktop::vector_masks {
namespace {

void set_error(QString* error, const QString& message) {
  if (error) *error = message;
}

bool fail(QString* error, const QString& message) {
  set_error(error, message);
  return false;
}

bool finite(const QPointF& point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

QPointF divide(const QPointF& point, const QSizeF& dimensions) {
  return {point.x() / dimensions.width(), point.y() / dimensions.height()};
}

QPointF multiply(const QPointF& point, const QSize& dimensions) {
  return {point.x() * static_cast<double>(dimensions.width()),
          point.y() * static_cast<double>(dimensions.height())};
}

}  // namespace

bool rebase_coordinates(const PhotoVectorMask& source,
                        CoordinateBasis target_basis,
                        const VectorMaskPlacement& placement,
                        const QSize& source_size,
                        PhotoVectorMask* rebased,
                        QString* error) {
  if (!rebased)
    return fail(error, QStringLiteral("A rebased vector mask output is required."));

  const auto source_error = validate(source);
  if (!source_error.isEmpty()) return fail(error, source_error);

  if (target_basis != CoordinateBasis::LocalLayer &&
      target_basis != CoordinateBasis::SourceDocument) {
    return fail(error, QStringLiteral("Choose local-layer or source-document coordinates."));
  }

  if (target_basis == source.basis) {
    *rebased = source;
    set_error(error, {});
    return true;
  }

  if (!source_size.isValid() || source_size.isEmpty())
    return fail(error, QStringLiteral("Source-document dimensions must be positive."));
  if (!placement.valid || !placement.local_to_source.isAffine() ||
      !placement.local_to_source.isInvertible()) {
    return fail(error, QStringLiteral("The selected layer placement cannot be inverted for mask coordinate conversion."));
  }

  bool invertible = false;
  const auto source_to_local = placement.local_to_source.inverted(&invertible);
  if (!invertible || !source_to_local.isAffine())
    return fail(error, QStringLiteral("The selected layer placement cannot be inverted for mask coordinate conversion."));

  const QSizeF path_dimensions = source.path.coordinate_space ==
          vector_paths::CoordinateSpace::Normalized
      ? QSizeF(1.0, 1.0)
      : source.path.document_size;
  if (!std::isfinite(path_dimensions.width()) ||
      !std::isfinite(path_dimensions.height()) ||
      path_dimensions.width() <= 0.0 || path_dimensions.height() <= 0.0) {
    return fail(error, QStringLiteral("The vector mask path dimensions are invalid."));
  }

  auto candidate = source;
  candidate.basis = target_basis;
  candidate.path.coordinate_space = vector_paths::CoordinateSpace::Normalized;
  candidate.path.document_size = QSizeF(0.0, 0.0);

  const auto convert = [&](const QPointF& point, QPointF* result) {
    const auto normalized = divide(point, path_dimensions);
    const auto source_pixel = source.basis == CoordinateBasis::LocalLayer
        ? placement.local_to_source.map(normalized)
        : multiply(normalized, source_size);
    const auto target_normalized = target_basis == CoordinateBasis::SourceDocument
        ? divide(source_pixel, QSizeF(static_cast<double>(source_size.width()),
                                      static_cast<double>(source_size.height())))
        : source_to_local.map(source_pixel);
    if (!finite(normalized) || !finite(source_pixel) ||
        !finite(target_normalized)) {
      return false;
    }
    *result = target_normalized;
    return true;
  };

  for (auto& subpath : candidate.path.subpaths) {
    for (auto& anchor : subpath.anchors) {
      QPointF position;
      QPointF incoming_control;
      QPointF outgoing_control;
      if (!convert(anchor.position, &position) ||
          !convert(anchor.position + anchor.incoming, &incoming_control) ||
          !convert(anchor.position + anchor.outgoing, &outgoing_control)) {
        return fail(error, QStringLiteral("The vector mask could not be mapped into the selected coordinate basis."));
      }
      anchor.position = position;
      anchor.incoming = incoming_control - position;
      anchor.outgoing = outgoing_control - position;
    }
  }

  const auto candidate_error = validate(candidate);
  if (!candidate_error.isEmpty()) {
    return fail(error, QStringLiteral("The converted vector mask exceeds the supported coordinate bounds: %1")
                               .arg(candidate_error));
  }

  *rebased = std::move(candidate);
  set_error(error, {});
  return true;
}

}  // namespace photo_editor::desktop::vector_masks
