#pragma once

#include "photo_vector_mask.h"
#include "photo_vector_mask_coordinates.h"

#include <QSize>
#include <QString>

namespace photo_editor::desktop::vector_masks {

// Changes a mask between layer-local and fixed source-document coordinates
// while keeping its geometry in the same source-pixel locations. The output is
// assigned only after the converted definition passes the path and mask
// schemas, so an unsupported or out-of-bounds conversion leaves it untouched.
bool rebase_coordinates(const PhotoVectorMask& source,
                        CoordinateBasis target_basis,
                        const VectorMaskPlacement& placement,
                        const QSize& source_size,
                        PhotoVectorMask* rebased,
                        QString* error = nullptr);

}  // namespace photo_editor::desktop::vector_masks
