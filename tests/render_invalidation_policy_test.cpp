#include "photo_editor/pipeline/render_invalidation_policy.h"

#include <cstdlib>
#include <iostream>

int main() {
  using namespace photo_editor::pipeline;
  const auto& order = preview_invalidation_stage_order();
  if (order.size() != 19U ||
      preview_invalidation_stage_name(order.front()) != "source_decode" ||
      preview_invalidation_stage_name(order.back()) != "histogram_scope") {
    std::cerr << "preview invalidation order must be complete and stable\n";
    return EXIT_FAILURE;
  }
  if (normalize_preview_dirty_domain(" Tone-Color ") !=
          "global_tone_color" ||
      normalize_preview_dirty_domain("mask composite") !=
          "mask_composition" ||
      preview_dirty_domain_for_reason("output_profile_change") !=
          "display_transform") {
    std::cerr << "dirty-domain aliases and render reasons must normalize centrally\n";
    return EXIT_FAILURE;
  }
  const auto output_only =
      preview_dirty_stages_for_domain("display_transform");
  const auto raw = preview_dirty_stages_for_domain("raw");
  const auto unknown = preview_dirty_stages_for_domain("unknown");
  if (output_only.size() != 5U ||
      preview_invalidation_stage_name(output_only.front()) !=
          "display_transform" ||
      raw.size() != order.size() - 1U ||
      preview_invalidation_stage_name(raw.front()) != "raw_demosaic" ||
      unknown != order) {
    std::cerr << "dirty domains must invalidate the exact downstream suffix\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
