#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

struct LensProfile final {
  std::string profile_name;
  std::string camera_make;
  std::string lens_model;
  double distortion = 0.0;
  double vignette = 0.0;
  double chromatic_aberration = 0.0;
};

[[nodiscard]] inline std::string normalized_lens_profile_key(std::string_view value) {
  std::string normalized(value);
  std::transform(
      normalized.begin(),
      normalized.end(),
      normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  normalized.erase(
      std::remove_if(
          normalized.begin(),
          normalized.end(),
          [](unsigned char ch) { return ch == '-' || ch == '_' || std::isspace(ch) != 0; }),
      normalized.end());
  return normalized;
}

[[nodiscard]] inline const std::vector<LensProfile>& known_lens_profiles() {
  static const std::vector<LensProfile> profiles = {
      {
          .profile_name = "Canon RF 28-70mm F2L USM",
          .camera_make = "Canon",
          .lens_model = "RF 28-70mm F2L USM",
          .distortion = 12.0,
          .vignette = 18.0,
          .chromatic_aberration = 22.0,
      },
      {
          .profile_name = "Canon RF 24-70mm F2.8L IS USM",
          .camera_make = "Canon",
          .lens_model = "RF 24-70mm F2.8L IS USM",
          .distortion = 10.0,
          .vignette = 16.0,
          .chromatic_aberration = 18.0,
      },
      {
          .profile_name = "NIKKOR Z 24-70mm f/2.8 S",
          .camera_make = "Nikon",
          .lens_model = "NIKKOR Z 24-70mm f/2.8 S",
          .distortion = 9.0,
          .vignette = 15.0,
          .chromatic_aberration = 14.0,
      },
      {
          .profile_name = "Sony FE 24-70mm F2.8 GM II",
          .camera_make = "Sony",
          .lens_model = "FE 24-70mm F2.8 GM II",
          .distortion = 8.0,
          .vignette = 14.0,
          .chromatic_aberration = 16.0,
      },
      {
          .profile_name = "FUJINON XF 33mm F1.4 R LM WR",
          .camera_make = "Fujifilm",
          .lens_model = "XF33mmF1.4 R LM WR",
          .distortion = 4.0,
          .vignette = 9.0,
          .chromatic_aberration = 8.0,
      },
      {
          .profile_name = "Apple iPhone Wide Camera",
          .camera_make = "Apple",
          .lens_model = "iPhone Wide Camera",
          .distortion = 7.0,
          .vignette = 10.0,
          .chromatic_aberration = 6.0,
      },
  };
  return profiles;
}

[[nodiscard]] inline const LensProfile* find_lens_profile(
    std::string_view camera_make,
    std::string_view lens_model) {
  const auto make_key = normalized_lens_profile_key(camera_make);
  const auto lens_key = normalized_lens_profile_key(lens_model);
  if (lens_key.empty()) {
    return nullptr;
  }

  for (const auto& profile : known_lens_profiles()) {
    const auto profile_lens_key = normalized_lens_profile_key(profile.lens_model);
    if (profile_lens_key == lens_key) {
      const auto profile_make_key = normalized_lens_profile_key(profile.camera_make);
      if (make_key.empty() || make_key == profile_make_key ||
          make_key.find(profile_make_key) != std::string::npos ||
          profile_make_key.find(make_key) != std::string::npos) {
        return &profile;
      }
    }
  }

  return nullptr;
}

}  // namespace photo_editor::pipeline
