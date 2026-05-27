#include "photo_editor/pipeline/lens_profile.h"

#include <cstdlib>
#include <iostream>

int main() {
  const auto& profiles = photo_editor::pipeline::known_lens_profiles();
  if (profiles.size() < 5) {
    std::cerr << "lens profile database should include a representative starter set\n";
    return EXIT_FAILURE;
  }

  const auto* canon_profile =
      photo_editor::pipeline::find_lens_profile("Canon", "RF 28-70mm F2L USM");
  if (canon_profile == nullptr || canon_profile->profile_name != "Canon RF 28-70mm F2L USM" ||
      canon_profile->distortion <= 0.0 || canon_profile->vignette <= 0.0 ||
      canon_profile->chromatic_aberration <= 0.0) {
    std::cerr << "lens profile lookup should return correction coefficients\n";
    return EXIT_FAILURE;
  }

  const auto* normalized_profile =
      photo_editor::pipeline::find_lens_profile("NIKON CORPORATION", "nikkor z 24 70mm f/2.8 s");
  if (normalized_profile == nullptr ||
      normalized_profile->profile_name != "NIKKOR Z 24-70mm f/2.8 S") {
    std::cerr << "lens profile lookup should tolerate punctuation and make aliases\n";
    return EXIT_FAILURE;
  }

  if (photo_editor::pipeline::find_lens_profile("Unknown", "Unknown Lens") != nullptr) {
    std::cerr << "unknown lenses should not match a correction profile\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
