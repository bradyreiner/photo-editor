#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

struct EditRecipeState;

[[nodiscard]] QVariantMap validate_agent_edit_image(
    const QImage& image,
    const QVariantMap& style = {});

[[nodiscard]] QVariantMap validate_agent_edit_image(
    const QImage& image,
    const EditRecipeState& recipe,
    const QVariantMap& style = {});

[[nodiscard]] QVariantMap evaluate_agent_edit_intent_alignment(
    const QVariantMap& before_image_stats,
    const QVariantMap& after_image_stats,
    const QString& instructions,
    const QVariantMap& edit_intent = {});

[[nodiscard]] QVariantMap evaluate_agent_retouch_proof_quality(
    const QImage& retouch_baseline,
    const QImage& after,
    const QVariantMap& materialized_patch);

}  // namespace photo_editor::desktop
