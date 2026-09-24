#pragma once

#include <QJsonValue>
#include <QString>

namespace photo_editor::desktop {

struct CatalogAdjustmentProjectionResult final {
  QString adjustment_patch_json;
  QString error;
  bool canonical_present = false;

  [[nodiscard]] bool ok() const noexcept {
    return error.isEmpty();
  }
};

[[nodiscard]] CatalogAdjustmentProjectionResult
project_catalog_adjustments_for_desktop(
    const QJsonValue& legacy_recipe,
    const QJsonValue& canonical_recipe,
    const QString& recipe_asset_root);

}  // namespace photo_editor::desktop
