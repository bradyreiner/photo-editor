#pragma once

#include "mask_alpha_provider.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <array>
#include <vector>

#include <QImage>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] MaskAlphaEvaluationResult evaluate_canonical_composite_mask_alpha(
    const QImage& source_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance);

[[nodiscard]] MaskAlphaEvaluationResult evaluate_canonical_composite_mask_alpha(
    const QString& cache_key,
    const QImage& source_image,
    const std::vector<photo_editor::pipeline::LocalMask>& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance);

[[nodiscard]] QVariantMap canonical_composite_mask_thumbnail_alpha_metadata(
    const QImage& source_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance);

}  // namespace photo_editor::desktop
