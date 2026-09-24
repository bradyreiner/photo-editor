#pragma once

#include "photo_editor/pipeline/edit_recipe_v13.h"

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <optional>

namespace photo_editor::desktop {

struct RecipeAssetWriteResult final {
  std::optional<
      photo_editor::pipeline::AssetIdentifier>
      asset;
  QString error;

  [[nodiscard]] bool ok() const noexcept {
    return asset.has_value() && error.isEmpty();
  }
};

class RecipeAssetStore final {
 public:
  explicit RecipeAssetStore(QString root_path);

  [[nodiscard]] RecipeAssetWriteResult put(
      const QByteArray& payload) const;
  [[nodiscard]] RecipeAssetWriteResult putVariant(
      const QVariant& value) const;
  [[nodiscard]] std::optional<QByteArray> get(
      const photo_editor::pipeline::AssetIdentifier&
          asset,
      QString* error = nullptr) const;
  [[nodiscard]] std::optional<QVariant> getVariant(
      const photo_editor::pipeline::AssetIdentifier&
          asset,
      QString* error = nullptr) const;
  [[nodiscard]] QString rootPath() const;

 private:
  [[nodiscard]] QString pathForDigest(
      const QString& digest) const;

  QString root_path_;
};

}  // namespace photo_editor::desktop
