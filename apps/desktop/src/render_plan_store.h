#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <memory>
#include <optional>

#include <QSize>
#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct CanonicalRenderPlanAdmission final {
  bool admitted = false;
  bool cache_hit = false;
  int cache_resident_entries = 0;
  quint64 cache_hit_count = 0;
  quint64 cache_build_count = 0;
  std::optional<photo_editor::pipeline::RenderPlan> plan;
  QString plan_cache_key;
  QString recipe_cache_key;
  QString serialized_plan;
  QString error;
};

class RenderPlanStore final {
 public:
  static RenderPlanStore& shared();

  [[nodiscard]] CanonicalRenderPlanAdmission admit(
      const QString& source_asset_id,
      const QString& source_path,
      const EditRecipeState& state,
      const QSize& source_size,
      photo_editor::pipeline::RenderPurpose purpose,
      photo_editor::pipeline::DisplayRenderingTarget rendering_target,
      const std::optional<photo_editor::pipeline::AssetIdentifier>&
          device_icc_profile = std::nullopt,
      const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
      const QString& recipe_payload_digest = {},
      photo_editor::pipeline::RenderScopeDomain scope_domain =
          photo_editor::pipeline::RenderScopeDomain::FinalDisplay);

  RenderPlanStore(const RenderPlanStore&) = delete;
  RenderPlanStore& operator=(const RenderPlanStore&) = delete;
  ~RenderPlanStore();

 private:
  struct Cache;
  RenderPlanStore();
  std::unique_ptr<Cache> cache_;
};

}  // namespace photo_editor::desktop
