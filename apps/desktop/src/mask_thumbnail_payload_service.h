#pragma once

#include <array>

#include <QImage>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

struct MaskAlphaEvaluationResult;

[[nodiscard]] QVariantMap mask_thumbnail_payload_from_alpha(
    const MaskAlphaEvaluationResult& alpha_result);

[[nodiscard]] QVariantMap canonical_mask_thumbnail_payload(
    const QImage& thumbnail_image,
    const QVariantList& masks,
    double exposure_gain,
    const std::array<double, 3>& white_balance);

}  // namespace photo_editor::desktop
