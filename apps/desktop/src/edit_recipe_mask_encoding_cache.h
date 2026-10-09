#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <array>

namespace photo_editor::desktop {
struct EditRecipeState;

// One journal-owned entry. Empty legacy stacks are deliberately never cached.
// The retained key owns its string/binary storage, including fromRawData inputs.
class EditRecipeMaskEncodingCache final {
 public:
  const std::array<QJsonArray, 3>* find(const QVariantList& masks) const;
  void store(const QVariantList& masks, const std::array<QJsonArray, 3>& encoded);
  void clear();

 private:
  QVariantList masks_;
  std::array<QJsonArray, 3> encoded_;
};

QJsonObject build_edit_recipe_object_with_mask_cache(
    const EditRecipeState& state, EditRecipeMaskEncodingCache* cache);
}  // namespace photo_editor::desktop
