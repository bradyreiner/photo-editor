#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_transform.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace photo_editor::pipeline {

enum class VideoInputTransformProfile {
  SonySLog2SGamut,
  SonySLog3SGamut3,
  SonySLog3SGamut3Cine,
};

struct CompiledVideoInputTransform final {
  VideoInputTransformProfile profile =
      VideoInputTransformProfile::SonySLog3SGamut3;
  RgbColorSpace working_space;
  ColorMatrix3 linear_source_to_working;
  std::string chromatic_adaptation_method = "Bradford";
};

struct CompiledVideoRec709OutputTransform final {
  RgbColorSpace working_space;
  ColorMatrix3 linear_working_to_rec709;
  std::string chromatic_adaptation_method = "Bradford";
};

// Source-stream tags are resolved once by the shared engine policy so the
// preview decoder and FFmpeg delivery adapter make the same defaults when
// metadata is missing or incomplete. Matrix/range/transfer/primaries values
// use FFmpeg's canonical lowercase tokens at the adapter boundary.
struct VideoSourceSignalMetadata final {
  std::string matrix;
  std::string range;
  std::string transfer;
  std::string primaries;
  int frame_height = 0;
};

struct ResolvedVideoSourceSignalMetadata final {
  std::string matrix;
  std::string range;
  std::string transfer;
  std::string primaries;
  std::string output_transfer;
  std::string output_primaries;
  double ycbcr_kr = 0.299;
  double ycbcr_kb = 0.114;
  bool source_is_hdr = false;
  bool source_is_rec709 = false;
  bool standard_matrix = false;
  bool metadata_aware_yuv_supported = true;
  bool restore_limited_camera_code_values = false;
};

struct VideoTransformLutSettings final {
  int cube_size = 2;
  int validation_grid_size = 23;
  double p99_absolute_error_limit = 0.0015;
  double maximum_absolute_error_limit = 0.005;
};

struct VideoTransformLutBuildResult final {
  ColorLut3D lut;
  std::array<double, 2> linear_rec709_output_range = {0.0, 1.0};
  std::size_t validation_sample_count = 0U;
  double measured_p99_absolute_error = 0.0;
  double measured_maximum_absolute_error = 0.0;
  std::string failure_reason;

  [[nodiscard]] bool ok() const {
    return lut.valid && failure_reason.empty();
  }
};

struct VideoRec709LutBuildSettings final {
  int cube_size = 65;
  int validation_grid_size = 23;
  double p99_absolute_error_limit = 0.0015;
  double maximum_absolute_error_limit = 0.005;
};

struct VideoRec709LutBuildResult final {
  ColorLut3D lut;
  std::size_t validation_sample_count = 0U;
  double measured_p99_absolute_error = 0.0;
  double measured_maximum_absolute_error = 0.0;
  std::string failure_reason;

  [[nodiscard]] bool ok() const {
    return lut.valid && failure_reason.empty();
  }
};

[[nodiscard]] double decode_sony_slog3(double code_value);
[[nodiscard]] double encode_sony_slog3(double scene_linear_reflection);
[[nodiscard]] double decode_sony_slog2(double code_value);
[[nodiscard]] double encode_sony_slog2(double scene_linear_reflection);
// ITU-R BT.709-6, 1.2 OETF for linear-light Rec.709 values. This preserves
// extended values; callers choose their tone and gamut mapping before encoding.
[[nodiscard]] double encode_rec709_oetf(double linear_light);

[[nodiscard]] ResolvedVideoSourceSignalMetadata
resolve_video_source_signal_metadata(
    const VideoSourceSignalMetadata& metadata,
    bool camera_input_transform_selected = false);

[[nodiscard]] std::optional<CompiledVideoInputTransform>
compile_video_input_transform(
    VideoInputTransformProfile profile,
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method = "Bradford");

[[nodiscard]] ColorPixel apply_video_input_transform(
    const CompiledVideoInputTransform& transform,
    const ColorPixel& encoded_pixel);
void apply_video_input_transform(
    const CompiledVideoInputTransform& transform,
    std::span<ColorPixel> encoded_pixels);

[[nodiscard]] std::optional<CompiledVideoRec709OutputTransform>
compile_video_rec709_output_transform(
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method = "Bradford");

// Converts a scene-linear working frame to Rec.709 primaries and BT.709 signal
// encoding in place, without allocating a second full-frame pixel buffer.
[[nodiscard]] ColorPixel apply_video_rec709_output_transform(
    const CompiledVideoRec709OutputTransform& transform,
    const ColorPixel& working_pixel);
void apply_video_rec709_output_transform(
    const CompiledVideoRec709OutputTransform& transform,
    std::span<ColorPixel> working_pixels);

// Compares a deterministic sample grid to the analytic transform and assumes
// the photo engine's trilinear apply_lut interpolation contract.
// Builds the selected Sony S-Log decode and camera-gamut conversion stages
// through linear Rec.709 primaries. Apply the BT.709 OETF after scene-referred
// grading.
[[nodiscard]] VideoTransformLutBuildResult
build_sony_video_input_linear_rec709_lut(
    VideoInputTransformProfile profile,
    VideoTransformLutSettings settings = {});
// Builds a bounded display-referred cube from normalized Sony camera code
// values through the shared photo-engine input and Rec.709 output transforms.
// The cube is valid only when tetrahedral interpolation passes the error gate.
[[nodiscard]] VideoRec709LutBuildResult
build_sony_video_input_rec709_lut(
    VideoInputTransformProfile profile,
    VideoRec709LutBuildSettings settings = {});
// The live monitor and timeline delivery share this profile-scoped, lazily
// built cube. It retains at most three validated 65^3 tables.
[[nodiscard]] const VideoRec709LutBuildResult&
cached_sony_video_input_rec709_lut(VideoInputTransformProfile profile);
// Compatibility entry point for the original S-Log3 profiles.
[[nodiscard]] VideoTransformLutBuildResult
build_sony_slog3_linear_rec709_lut(
    VideoInputTransformProfile profile,
    VideoTransformLutSettings settings = {});

}  // namespace photo_editor::pipeline
