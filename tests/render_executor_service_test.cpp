#include "render_executor_service.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using photo_editor::desktop::export_render_executor;
  using photo_editor::desktop::preview_render_executor;

  auto* const first_preview = &preview_render_executor();
  auto* const second_preview = &preview_render_executor();
  auto* const first_export = &export_render_executor();
  auto* const second_export = &export_render_executor();

  if (first_preview != second_preview) {
    return fail("preview rendering must retain one process-wide executor");
  }
  if (first_export != second_export) {
    return fail("export rendering must retain one process-wide executor");
  }
  if (first_preview == first_export) {
    return fail("interactive preview and final export must keep independent cache budgets");
  }
  if (photo_editor::desktop::kPreviewRenderExecutorCacheBytes !=
          384ULL * 1024ULL * 1024ULL ||
      photo_editor::desktop::kExportRenderExecutorCacheBytes !=
          512ULL * 1024ULL * 1024ULL) {
    return fail("render executor cache budgets changed without updating the service contract");
  }

  photo_editor::desktop::prewarm_default_aces2_output_luts();
  photo_editor::desktop::prewarm_default_aces2_output_luts();
  return EXIT_SUCCESS;
}
