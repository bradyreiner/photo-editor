#include "edit_recipe_content_validation.h"

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

void validate_edit_recipe_content_contract(
    const QJsonObject& recipe,
    QStringList* errors) {
  if (errors == nullptr) {
    return;
  }

  const auto history = recipe.value(QStringLiteral("history")).toObject();
  if (!history.value(QStringLiteral("activeBranchId")).isString()) {
    errors->append(
        QStringLiteral("history.activeBranchId must be a string"));
  }
  if (!history.value(QStringLiteral("snapshots")).isArray()) {
    errors->append(QStringLiteral("history.snapshots must be an array"));
  }
  const auto snapshots = history.value(QStringLiteral("snapshots")).toArray();
  for (int index = 0; index < snapshots.size(); ++index) {
    if (!snapshots.at(index).isObject()) {
      errors->append(
          QStringLiteral("history.snapshots[%1] must be an object").arg(index));
      continue;
    }
    const auto snapshot = snapshots.at(index).toObject();
    if (!snapshot.value(QStringLiteral("id")).isString()) {
      errors->append(
          QStringLiteral("history.snapshots[%1].id must be a string")
              .arg(index));
    }
    if (!snapshot.value(QStringLiteral("name")).isString()) {
      errors->append(
          QStringLiteral("history.snapshots[%1].name must be a string")
              .arg(index));
    }
    if (!snapshot.value(QStringLiteral("branchId")).isString()) {
      errors->append(
          QStringLiteral("history.snapshots[%1].branchId must be a string")
              .arg(index));
    }
    const auto adjustment_patch =
        snapshot.value(QStringLiteral("adjustmentPatch"));
    const auto legacy_recipe =
        snapshot.value(QStringLiteral("recipeJson"));
    if (!adjustment_patch.isObject() && !legacy_recipe.isString()) {
      errors->append(
          QStringLiteral(
              "history.snapshots[%1].adjustmentPatch must be an object")
              .arg(index));
    } else if (adjustment_patch.isObject()) {
      const auto patch = adjustment_patch.toObject();
      if (patch.value(QStringLiteral("format")).toString() !=
              QStringLiteral("photo-editor-adjustment-patch") ||
          patch.value(QStringLiteral("version")).toInt() != 1 ||
          patch.value(QStringLiteral("scope")).toString() !=
              QStringLiteral("asset-bound") ||
          !patch.value(QStringLiteral("groups")).isArray() ||
          !patch.value(QStringLiteral("adjustmentState")).isObject()) {
        errors->append(
            QStringLiteral(
                "history.snapshots[%1].adjustmentPatch must be a valid asset-bound v1 patch")
                .arg(index));
      }
    }
  }

  const auto mask_stack = recipe.value(QStringLiteral("maskStack")).toArray();
  for (int index = 0; index < mask_stack.size(); ++index) {
    if (!mask_stack.at(index).isObject()) {
      errors->append(
          QStringLiteral("maskStack[%1] must be an object").arg(index));
      continue;
    }
    const auto mask = mask_stack.at(index).toObject();
    if (!mask.value(QStringLiteral("kind")).isString()) {
      errors->append(
          QStringLiteral("maskStack[%1].kind must be a string").arg(index));
    }
    if (!mask.value(QStringLiteral("operation")).isString()) {
      errors->append(
          QStringLiteral("maskStack[%1].operation must be a string")
              .arg(index));
    }
    if (!mask.value(QStringLiteral("adjustments")).isObject()) {
      errors->append(
          QStringLiteral("maskStack[%1].adjustments must be an object")
              .arg(index));
    }
    if (!mask.value(QStringLiteral("geometry")).isObject()) {
      errors->append(
          QStringLiteral("maskStack[%1].geometry must be an object")
              .arg(index));
      continue;
    }

    const auto geometry = mask.value(QStringLiteral("geometry")).toObject();
    const auto validate_geometry_payload =
        [&](const QString& data_key,
            const QString& width_key,
            const QString& height_key,
            const QString& label) {
          const auto payload = geometry.value(data_key).toString();
          if (payload.isEmpty()) {
            return;
          }
          if (!valid_segmentation_payload(
                  QByteArray::fromBase64(payload.toLatin1()),
                  geometry.value(width_key).toInt(),
                  geometry.value(height_key).toInt())) {
            errors->append(
                QStringLiteral(
                    "maskStack[%1].geometry %2 payload size does not match dimensions or exceeds bounds")
                    .arg(index)
                    .arg(label));
          }
        };
    validate_geometry_payload(
        QStringLiteral("segmentationData"),
        QStringLiteral("segmentationWidth"),
        QStringLiteral("segmentationHeight"),
        QStringLiteral("segmentation"));
    validate_geometry_payload(
        QStringLiteral("confidenceData"),
        QStringLiteral("confidenceWidth"),
        QStringLiteral("confidenceHeight"),
        QStringLiteral("confidence"));
    validate_geometry_payload(
        QStringLiteral("depthData"),
        QStringLiteral("depthWidth"),
        QStringLiteral("depthHeight"),
        QStringLiteral("depth"));
    validate_geometry_payload(
        QStringLiteral("inpaintSourceMaskData"),
        QStringLiteral("inpaintSourceMaskWidth"),
        QStringLiteral("inpaintSourceMaskHeight"),
        QStringLiteral("inpaint source mask"));
  }
}

}  // namespace photo_editor::desktop
