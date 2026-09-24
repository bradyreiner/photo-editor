#pragma once

namespace photo_editor::pipeline {

struct CieXyz final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct CieLab final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
};

struct LinearRgb final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

struct IctcpColor final {
  double intensity = 0.0;
  double tritan = 0.0;
  double protan = 0.0;
};

enum class ChromaticAdaptationMethod {
  Bradford,
  Cat02,
};

[[nodiscard]] constexpr CieXyz cie_d50_white() {
  return CieXyz{.x = 0.96422, .y = 1.0, .z = 0.82521};
}

[[nodiscard]] constexpr CieXyz cie_d65_white() {
  return CieXyz{.x = 0.95047, .y = 1.0, .z = 1.08883};
}

[[nodiscard]] CieLab cie_xyz_to_lab(
    const CieXyz& xyz,
    const CieXyz& reference_white = cie_d50_white());
[[nodiscard]] CieXyz cie_lab_to_xyz(
    const CieLab& lab,
    const CieXyz& reference_white = cie_d50_white());
[[nodiscard]] CieXyz chromatically_adapt_xyz(
    const CieXyz& xyz,
    const CieXyz& source_white,
    const CieXyz& target_white,
    ChromaticAdaptationMethod method);

[[nodiscard]] double delta_e_2000(
    const CieLab& left,
    const CieLab& right,
    double lightness_weight = 1.0,
    double chroma_weight = 1.0,
    double hue_weight = 1.0);

[[nodiscard]] double st2084_encode_nits(double luminance_nits);
[[nodiscard]] IctcpColor rec2020_nits_to_ictcp(const LinearRgb& rgb_nits);
[[nodiscard]] double delta_e_itp(const IctcpColor& left, const IctcpColor& right);

}  // namespace photo_editor::pipeline
