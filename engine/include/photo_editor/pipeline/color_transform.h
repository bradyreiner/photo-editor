#pragma once

#include "photo_editor/pipeline/color_space.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

inline constexpr std::size_t kMaximumIccProfileBytes = 16U * 1024U * 1024U;

struct ColorChromaticity final {
  double x = 0.0;
  double y = 0.0;
};

struct ColorXyz final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct ColorMatrix3 final {
  bool operator==(const ColorMatrix3&) const = default;
  std::array<double, 9> values = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
};

struct CompiledBuiltinColorTransform final {
  ColorMatrix3 matrix;
  RgbColorSpace source;
  RgbColorSpace destination;
  std::string chromatic_adaptation_method = "Bradford";
  std::string provenance = "builtin-matrix:v1";
};

enum class IccRenderingIntent {
  Perceptual,
  RelativeColorimetric,
  Saturation,
  AbsoluteColorimetric,
};

enum class IccTransformStatus {
  Ready,
  BackendUnavailable,
  ProfilePayloadTooLarge,
  InvalidSourceProfile,
  InvalidDestinationProfile,
  InvalidProofProfile,
  UnsupportedSourceProfile,
  UnsupportedDestinationProfile,
  UnsupportedProofProfile,
  CompileFailed,
};

enum class IccPixelFormat {
  RgbaFloat32,
};

struct IccTransformOptions final {
  IccRenderingIntent rendering_intent = IccRenderingIntent::Perceptual;
  bool black_point_compensation = true;
  IccPixelFormat pixel_format = IccPixelFormat::RgbaFloat32;
  std::string target = "unspecified";
};

struct IccProofTransformOptions final {
  IccRenderingIntent rendering_intent = IccRenderingIntent::RelativeColorimetric;
  IccRenderingIntent proofing_intent = IccRenderingIntent::RelativeColorimetric;
  bool black_point_compensation = true;
  bool gamut_check = false;
  IccPixelFormat pixel_format = IccPixelFormat::RgbaFloat32;
  std::string target = "soft-proof";
};

struct IccRgbaFloat final {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 1.0F;
};

struct IccTransformDescription final {
  IccTransformStatus status = IccTransformStatus::BackendUnavailable;
  std::string backend;
  std::string backend_version;
  std::string source_profile_description;
  std::string destination_profile_description;
  std::string proof_profile_description;
  std::string source_profile_sha256;
  std::string destination_profile_sha256;
  std::string proof_profile_sha256;
  std::string cache_key;
  std::string target;
  std::string failure_reason;
  bool device_link = false;
  bool soft_proofing = false;
  bool gamut_check = false;
  bool rendering_intent_baked = false;
  IccRenderingIntent rendering_intent = IccRenderingIntent::Perceptual;
  IccRenderingIntent proofing_intent = IccRenderingIntent::RelativeColorimetric;
  bool black_point_compensation = true;
  IccPixelFormat pixel_format = IccPixelFormat::RgbaFloat32;
};

struct IccTransformCacheStats final {
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t evictions = 0;
  std::size_t entries = 0;
  std::size_t capacity = 0;
};

enum class IccTransformLutQuality {
  Interactive,
  Final,
};

struct IccTransformLutSettings final {
  int cube_size = 0;
  double input_shaper_exponent = 1.0 / 3.0;
  double p99_delta_e_00_limit = 0.0;
  double maximum_delta_e_00_limit = 0.0;
  double maximum_unsafe_cell_fraction = 0.05;
};

struct IccTransformLut3D final {
  std::string schema = "icc-transform-lut:v1";
  std::string transform_cache_key;
  std::string cache_key;
  IccTransformLutSettings settings;
  std::vector<IccRgbaFloat> table;
  std::vector<std::uint8_t> unsafe_cells;
  std::size_t unsafe_cell_count = 0U;
  std::size_t validation_sample_count = 0U;
  double measured_p99_delta_e_00 = 0.0;
  double measured_maximum_delta_e_00 = 0.0;
  double measured_maximum_channel_error = 0.0;

  [[nodiscard]] bool valid() const;
  [[nodiscard]] bool accepts(const IccRgbaFloat& pixel) const;
  [[nodiscard]] std::size_t byte_size() const;
};

struct IccTransformLutCacheStats final {
  std::uint64_t requests = 0U;
  std::uint64_t hits = 0U;
  std::uint64_t misses = 0U;
  std::uint64_t builds = 0U;
  std::uint64_t rejected = 0U;
  std::uint64_t evictions = 0U;
  std::size_t entries = 0U;
  std::size_t bytes = 0U;
  std::size_t capacity = 0U;
  std::size_t maximum_bytes = 0U;
  std::string last_failure_reason;
};

struct IccTransformLutApplyStats final {
  std::size_t lut_pixels = 0U;
  std::size_t exact_fallback_pixels = 0U;
};

struct IccBackendCapability final {
  bool available = false;
  bool strict_version = false;
  std::string backend;
  std::string compiled_version;
  std::string runtime_version;
  std::string validated_version;
  std::string failure_reason;
};

class CompiledIccTransform final {
 public:
  struct Impl;

  CompiledIccTransform();
  ~CompiledIccTransform();
  CompiledIccTransform(const CompiledIccTransform&);
  CompiledIccTransform& operator=(const CompiledIccTransform&);
  CompiledIccTransform(CompiledIccTransform&&) noexcept;
  CompiledIccTransform& operator=(CompiledIccTransform&&) noexcept;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] const IccTransformDescription& description() const;
  [[nodiscard]] bool apply(std::span<IccRgbaFloat> pixels) const;
  [[nodiscard]] std::vector<std::uint8_t> gamut_mask(
      std::span<const IccRgbaFloat> pixels) const;

 private:
  explicit CompiledIccTransform(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;

  friend CompiledIccTransform compile_icc_transform(
      std::span<const std::byte>,
      std::span<const std::byte>,
      IccRenderingIntent,
      bool);
  friend CompiledIccTransform compile_icc_transform(
      std::span<const std::byte>,
      std::span<const std::byte>,
      const IccTransformOptions&);
  friend CompiledIccTransform compile_icc_device_link(
      std::span<const std::byte>,
      const IccTransformOptions&);
  friend CompiledIccTransform compile_icc_proofing_transform(
      std::span<const std::byte>,
      std::span<const std::byte>,
      std::span<const std::byte>,
      const IccProofTransformOptions&);
  friend std::shared_ptr<const IccTransformLut3D>
  compile_icc_transform_lut(
      const CompiledIccTransform&,
      IccTransformLutSettings);
};

[[nodiscard]] ColorMatrix3 identity_matrix3();
[[nodiscard]] double srgb_to_linear(double value);
[[nodiscard]] double linear_to_srgb(double value);
// Academy ACEScct equations operate component-wise between linear AP1 and the
// transient 32-bit grading encoding. ACEScct is not an interchange encoding.
[[nodiscard]] double linear_ap1_to_acescct(double value);
[[nodiscard]] double acescct_to_linear_ap1(double value);
[[nodiscard]] ColorMatrix3 matrix3_from_rows(
    double m00,
    double m01,
    double m02,
    double m10,
    double m11,
    double m12,
    double m20,
    double m21,
    double m22);
[[nodiscard]] ColorMatrix3 multiply_matrix3(
    const ColorMatrix3& left,
    const ColorMatrix3& right);
[[nodiscard]] std::array<double, 3> multiply_matrix3_vector(
    const ColorMatrix3& matrix,
    const std::array<double, 3>& vector);
[[nodiscard]] ColorXyz multiply_matrix3_xyz(
    const ColorMatrix3& matrix,
    const ColorXyz& xyz);
[[nodiscard]] ColorMatrix3 invert_matrix3(const ColorMatrix3& matrix);

[[nodiscard]] ColorXyz xyz_from_chromaticity(const ColorChromaticity& chromaticity);
[[nodiscard]] ColorChromaticity white_point_chromaticity(WhitePoint white_point);
[[nodiscard]] std::array<ColorChromaticity, 3> rgb_primaries(ColorPrimaries primaries);
[[nodiscard]] ColorMatrix3 rgb_to_xyz_matrix(const RgbColorSpace& color_space);
[[nodiscard]] ColorMatrix3 chromatic_adaptation_cone_matrix(std::string_view method);
[[nodiscard]] ColorMatrix3 chromatic_adaptation_matrix(
    const ColorXyz& source_white,
    const ColorXyz& target_white,
    std::string_view method);
[[nodiscard]] CompiledBuiltinColorTransform compile_builtin_color_transform(
    const RgbColorSpace& source,
    const RgbColorSpace& destination,
    std::string_view chromatic_adaptation_method = "Bradford");
[[nodiscard]] CompiledIccTransform compile_icc_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> destination_profile,
    IccRenderingIntent rendering_intent = IccRenderingIntent::Perceptual,
    bool black_point_compensation = true);
[[nodiscard]] CompiledIccTransform compile_icc_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> destination_profile,
    const IccTransformOptions& options);
[[nodiscard]] std::vector<std::byte> builtin_rgb_icc_profile_payload(
    const RgbColorSpace& color_space);
[[nodiscard]] CompiledIccTransform compile_builtin_to_icc_transform(
    const RgbColorSpace& source_color_space,
    std::span<const std::byte> destination_profile,
    const IccTransformOptions& options = {});
[[nodiscard]] CompiledIccTransform compile_icc_to_builtin_transform(
    std::span<const std::byte> source_profile,
    const RgbColorSpace& destination_color_space,
    const IccTransformOptions& options = {});
[[nodiscard]] CompiledIccTransform compile_icc_device_link(
    std::span<const std::byte> device_link_profile,
    const IccTransformOptions& options = {});
[[nodiscard]] CompiledIccTransform compile_icc_proofing_transform(
    std::span<const std::byte> source_profile,
    std::span<const std::byte> proof_profile,
    std::span<const std::byte> destination_profile,
    const IccProofTransformOptions& options = {});
[[nodiscard]] std::string color_payload_sha256(std::span<const std::byte> payload);
// Hashes incrementally so provenance checks do not require loading an entire
// profile, LUT, or archive into memory. Returns an empty string on I/O failure.
[[nodiscard]] std::string color_file_sha256(
    const std::filesystem::path& path);
[[nodiscard]] IccTransformCacheStats icc_transform_cache_stats();
[[nodiscard]] IccTransformLutSettings icc_transform_lut_settings(
    IccTransformLutQuality quality);
[[nodiscard]] std::shared_ptr<const IccTransformLut3D>
compile_icc_transform_lut(
    const CompiledIccTransform& transform,
    IccTransformLutSettings settings);
[[nodiscard]] std::shared_ptr<const IccTransformLut3D>
find_cached_icc_transform_lut(
    const CompiledIccTransform& transform,
    IccTransformLutSettings settings);
[[nodiscard]] bool apply_icc_transform_lut(
    const IccTransformLut3D& lut,
    std::span<IccRgbaFloat> pixels);
[[nodiscard]] bool apply_icc_transform_lut_hybrid(
    const IccTransformLut3D& lut,
    const CompiledIccTransform& exact_transform,
    std::span<IccRgbaFloat> pixels,
    IccTransformLutApplyStats* stats = nullptr);
[[nodiscard]] IccTransformLutCacheStats icc_transform_lut_cache_stats();
void clear_icc_transform_lut_cache_for_testing();
[[nodiscard]] IccBackendCapability icc_backend_capability();
void clear_icc_transform_cache();

}  // namespace photo_editor::pipeline
