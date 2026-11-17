#pragma once

#include "edit_recipe_state.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtGlobal>

#include <optional>

namespace photo_editor::desktop {

enum class AdjustmentPatchScope {
  SourceAgnostic,
  CameraBound,
  AssetBound,
};

struct AdjustmentPatchDecodeResult final {
  std::optional<EditRecipeState> state;
  QStringList groups;
  AdjustmentPatchScope scope =
      AdjustmentPatchScope::SourceAgnostic;
  QString error;

  [[nodiscard]] bool ok() const noexcept {
    return state.has_value() && error.isEmpty();
  }
};

struct StoredAdjustmentState final {
  std::optional<EditRecipeState> state;
  QStringList groups;
  bool legacy_compatibility_read = false;
};

inline constexpr qint64 kMaxBrushHistoryIdentityCacheMemoryBytes =
    64 * 1024 * 1024;
inline constexpr int kMaxBrushHistoryIdentityCacheEntries = 32;

struct BrushHistoryIdentityCacheSnapshot final {
  int entries = 0;
  qint64 retained_bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;
};

[[nodiscard]] QJsonObject serialize_adjustment_patch(
    const EditRecipeState& state,
    const QStringList& groups,
    AdjustmentPatchScope scope);

[[nodiscard]] QString serialize_adjustment_patch_json(
    const EditRecipeState& state,
    const QStringList& groups,
    AdjustmentPatchScope scope);

[[nodiscard]] QVariantMap build_reversible_preset_application_record(
    const EditRecipeState& before,
    const EditRecipeState& after,
    const QString& application_id,
    const QString& preset_identity,
    const QString& source,
    const QStringList& groups,
    double intensity);

[[nodiscard]] QString adjustment_patch_identity_json(
    const EditRecipeState& state);

// Internal cache diagnostics used by focused identity-payload tests.
[[nodiscard]] BrushHistoryIdentityCacheSnapshot
brush_history_identity_cache_snapshot();

[[nodiscard]] AdjustmentPatchDecodeResult
parse_adjustment_patch(const QJsonObject& patch);

[[nodiscard]] AdjustmentPatchDecodeResult
parse_adjustment_patch_json(const QString& patch_json);

[[nodiscard]] AdjustmentPatchDecodeResult
parse_adjustment_payload_or_legacy_recipe(
    const QString& payload);

[[nodiscard]] QString
canonical_asset_bound_adjustment_patch_json(
    const QString& payload);

[[nodiscard]] StoredAdjustmentState
parse_stored_adjustment_state(
    const QJsonObject& object,
    AdjustmentPatchScope required_scope);

[[nodiscard]] EditRecipeState
source_agnostic_adjustment_state(
    const EditRecipeState& state,
    const QStringList& groups);

}  // namespace photo_editor::desktop
