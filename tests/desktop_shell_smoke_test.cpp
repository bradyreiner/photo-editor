#include <cstdlib>
#include <iostream>

#include "desktop_shell_smoke_runner.h"

int main(int argc, char** argv) {
  const auto explicit_monolithic_soak =
      std::getenv("PHOTO_EDITOR_RUN_MONOLITHIC_SMOKE") != nullptr;
  const auto targeted_visual_proof =
      std::getenv("PHOTO_EDITOR_SMOKE_STOP_AFTER_PROFESSIONAL_LIBRARY") != nullptr ||
      std::getenv("PHOTO_EDITOR_SMOKE_STOP_AFTER_PRESET_LIBRARY") != nullptr ||
      std::getenv("PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE") != nullptr ||
      std::getenv("PHOTO_EDITOR_SMOKE_STOP_AFTER_PIXEL_ZOOM") != nullptr ||
      std::getenv("PHOTO_EDITOR_SMOKE_STOP_AFTER_MASK_GEOMETRY_PROOFS") != nullptr;
  if (!explicit_monolithic_soak && !targeted_visual_proof) {
    std::cerr
        << "desktop_shell_smoke_test is an explicit manual soak; use the focused desktop shell "
           "shards for deterministic CI, or set PHOTO_EDITOR_RUN_MONOLITHIC_SMOKE=1\n";
    return 77;
  }
  return run_desktop_shell_smoke(argc, argv);
}
