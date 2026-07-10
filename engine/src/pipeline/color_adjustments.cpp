#include "photo_editor/pipeline/color_adjustments.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
metal::MetalPipelineBackend& metal_backend() {
  static metal::MetalPipelineBackend backend;
  return backend;
}
#endif

constexpr double kMiddleGray = 0.18;
constexpr double kPi = std::numbers::pi_v<double>;

thread_local bool g_parallel_worker_active = false;

struct EdgeAwareMatteContext final {
  const std::vector<ColorPixel>* pixels = nullptr;
  int width = 0;
  int height = 0;
};

class ColorAdjustmentScheduler final {
 public:
  ColorAdjustmentScheduler() {
    const auto hardware_threads =
        std::max<unsigned int>(1, std::thread::hardware_concurrency());
    const auto worker_count = hardware_threads > 1 ? hardware_threads - 1 : 0;
    workers_.reserve(worker_count);
    for (auto worker_index = 0U; worker_index < worker_count; ++worker_index) {
      workers_.emplace_back([this]() { workerLoop(); });
    }
  }

  ColorAdjustmentScheduler(const ColorAdjustmentScheduler&) = delete;
  ColorAdjustmentScheduler& operator=(const ColorAdjustmentScheduler&) = delete;

  ~ColorAdjustmentScheduler() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    work_available_.notify_all();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  [[nodiscard]] std::size_t workerCount() const { return workers_.size(); }

  void run(int count, const std::function<void(int)>& work) {
    if (count <= 0) {
      return;
    }
    if (count < kSerialThreshold || workerCount() == 0 || g_parallel_worker_active) {
      runSerial(count, work);
      return;
    }

    const auto item_count = static_cast<std::size_t>(count);
    const auto max_task_count = std::max<std::size_t>(1, workerCount() + 1);
    const auto task_count = std::min<std::size_t>(
        max_task_count,
        std::max<std::size_t>(1, (item_count + kMinItemsPerTask - 1) / kMinItemsPerTask));
    if (task_count <= 1) {
      runSerial(count, work);
      return;
    }

    auto batch = std::make_shared<BatchState>();
    batch->remaining = task_count;

    {
      std::lock_guard lock(mutex_);
      for (std::size_t task_index = 1; task_index < task_count; ++task_index) {
        const auto begin = item_count * task_index / task_count;
        const auto end = item_count * (task_index + 1) / task_count;
        tasks_.push_back(Task{
            .begin = begin,
            .end = end,
            .work = work,
            .batch = batch,
        });
      }
    }
    work_available_.notify_all();

    try {
      const auto begin = std::size_t{0};
      const auto end = item_count / task_count;
      for (std::size_t index = begin; index < end; ++index) {
        work(static_cast<int>(index));
      }
    } catch (...) {
      std::lock_guard batch_lock(batch->mutex);
      if (batch->exception == nullptr) {
        batch->exception = std::current_exception();
      }
    }

    {
      std::lock_guard batch_lock(batch->mutex);
      --batch->remaining;
      if (batch->remaining == 0) {
        batch->completed.notify_one();
      }
    }

    std::unique_lock batch_lock(batch->mutex);
    batch->completed.wait(batch_lock, [&batch]() { return batch->remaining == 0; });
    if (batch->exception != nullptr) {
      std::rethrow_exception(batch->exception);
    }
  }

 private:
  static constexpr int kSerialThreshold = 16;
  static constexpr std::size_t kMinItemsPerTask = 8;

  struct BatchState final {
    std::mutex mutex;
    std::condition_variable completed;
    std::size_t remaining = 0;
    std::exception_ptr exception;
  };

  struct Task final {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::function<void(int)> work;
    std::shared_ptr<BatchState> batch;
  };

  static void runSerial(int count, const std::function<void(int)>& work) {
    for (int index = 0; index < count; ++index) {
      work(index);
    }
  }

  void workerLoop() {
    while (true) {
      Task task;
      {
        std::unique_lock lock(mutex_);
        work_available_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }

      try {
        g_parallel_worker_active = true;
        for (std::size_t index = task.begin; index < task.end; ++index) {
          task.work(static_cast<int>(index));
        }
      } catch (...) {
        std::lock_guard batch_lock(task.batch->mutex);
        if (task.batch->exception == nullptr) {
          task.batch->exception = std::current_exception();
        }
      }
      g_parallel_worker_active = false;

      {
        std::lock_guard batch_lock(task.batch->mutex);
        --task.batch->remaining;
        if (task.batch->remaining == 0) {
          task.batch->completed.notify_one();
        }
      }
    }
  }

  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable work_available_;
  std::deque<Task> tasks_;
  bool stopping_ = false;
};

ColorAdjustmentScheduler& color_adjustment_scheduler() {
  static ColorAdjustmentScheduler scheduler;
  return scheduler;
}

template <typename Work>
void parallel_for(int count, Work&& work) {
  color_adjustment_scheduler().run(count, std::function<void(int)>(std::forward<Work>(work)));
}

using StageClock = std::chrono::steady_clock;

[[nodiscard]] std::int64_t elapsed_stage_ms(StageClock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(StageClock::now() - start).count();
}

struct OklabColor final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
};

struct CurveEvaluationData final {
  std::vector<CurvePoint> points;
  std::vector<double> tangents;
};

struct LocalAdjustmentSettings final {
  double exposure = 0.0;
  double contrast = 0.0;
  double highlights = 0.0;
  double shadows = 0.0;
  double whites = 0.0;
  double blacks = 0.0;
  double temperature = 0.0;
  double tint = 0.0;
  double vibrance = 0.0;
  double saturation = 0.0;
  double hue_shift = 0.0;
  double saturation_shift = 0.0;
  double colorize_hue = 0.0;
  double colorize_strength = 0.0;
  double remove_strength = 0.0;
};

struct Chromaticity final {
  double x = 0.0;
  double y = 0.0;
};

struct XyzColor final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Matrix3 final {
  std::array<double, 9> values = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
};

struct PipelineColorContext final {
  RgbColorSpace working_space = make_linear_srgb_color_space();
  RenderIntent render_intent = RenderIntent::Perceptual;
  bool output_gamut_mapping_enabled = true;
  GamutMappingPolicy output_gamut_mapping_policy = GamutMappingPolicy::Perceptual;
  double output_headroom = 1.0;
  std::string chromatic_adaptation_method = "Bradford";
  Matrix3 rgb_to_xyz_native;
  Matrix3 xyz_native_to_rgb;
  Matrix3 rgb_to_xyz_d65;
  Matrix3 xyz_d65_to_rgb;
  Matrix3 rgb_to_linear_srgb;
};

[[nodiscard]] bool color_lut_shape_valid(const ColorLut3D& lut) {
  if (!lut.valid || lut.size < 2) {
    return false;
  }
  const auto size = static_cast<std::size_t>(lut.size);
  return lut.table.size() == size * size * size;
}

struct UserLutRuntime final {
  ColorLut3D lut;
  double strength = 0.0;
};

[[nodiscard]] ColorLut3D make_user_lut(
    int size,
    const std::array<double, 3>& domain_min,
    const std::array<double, 3>& domain_max,
    const std::vector<ColorPixel>& table,
    std::string_view input_color_space,
    std::string_view output_color_space) {
  ColorLut3D lut;
  lut.size = size;
  lut.domain_min = domain_min;
  lut.domain_max = domain_max;
  lut.table = table;
  lut.input_color_space = std::string(input_color_space);
  lut.output_color_space = std::string(output_color_space);
  lut.valid = true;
  if (!color_lut_shape_valid(lut)) {
    lut.valid = false;
  }
  return lut;
}

[[nodiscard]] std::vector<UserLutRuntime> user_luts_from_state(const ColorAdjustmentState& state) {
  std::vector<UserLutRuntime> luts;
  const auto single_strength = clamp_unit(state.lut_strength / 100.0);
  if (state.lut_enabled && single_strength > 1e-6) {
    auto lut = make_user_lut(
        state.lut_size,
        state.lut_domain_min,
        state.lut_domain_max,
        state.lut_table,
        state.lut_input_color_space,
        state.lut_output_color_space);
    if (color_lut_shape_valid(lut)) {
      luts.push_back(UserLutRuntime{.lut = std::move(lut), .strength = single_strength});
    }
  }
  for (const auto& adjustment : state.lut_stack) {
    const auto strength = clamp_unit(adjustment.strength / 100.0);
    if (!adjustment.enabled || strength <= 1e-6) {
      continue;
    }
    auto lut = make_user_lut(
        adjustment.size,
        adjustment.domain_min,
        adjustment.domain_max,
        adjustment.table,
        adjustment.input_color_space,
        adjustment.output_color_space);
    if (color_lut_shape_valid(lut)) {
      luts.push_back(UserLutRuntime{.lut = std::move(lut), .strength = strength});
    }
  }
  return luts;
}

struct InputStageParameters final {
  Matrix3 white_balance{};
  Matrix3 camera_profile{};
  Matrix3 camera_matching_look{};
  Matrix3 calibration{};
  double exposure_gain = 1.0;
  double highlight_reconstruction_strength = 0.0;
  double raw_black_level = 0.0;
  double raw_white_level = 1.0;
};

[[nodiscard]] Matrix3 identity_matrix3() {
  return {};
}

[[nodiscard]] XyzColor xyz_from_chromaticity(const Chromaticity& chromaticity) {
  const auto safe_y = std::max(1e-6, chromaticity.y);
  return XyzColor{
      .x = chromaticity.x / safe_y,
      .y = 1.0,
      .z = (1.0 - chromaticity.x - chromaticity.y) / safe_y,
  };
}

[[nodiscard]] Matrix3 matrix3_from_rows(
    double m00, double m01, double m02,
    double m10, double m11, double m12,
    double m20, double m21, double m22) {
  return Matrix3{
      .values = {
          m00, m01, m02,
          m10, m11, m12,
          m20, m21, m22,
      },
  };
}

[[nodiscard]] Matrix3 multiply_matrix3(const Matrix3& left, const Matrix3& right) {
  Matrix3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      auto value = 0.0;
      for (int k = 0; k < 3; ++k) {
        value += left.values[static_cast<std::size_t>(row * 3 + k)] *
                 right.values[static_cast<std::size_t>(k * 3 + column)];
      }
      result.values[static_cast<std::size_t>(row * 3 + column)] = value;
    }
  }
  return result;
}

[[nodiscard]] Matrix3 calibration_saturation_matrix(double saturation) {
  const auto strength = clamp_signed_percent(saturation) / 100.0;
  const auto scale = std::max(0.0, 1.0 + strength);
  constexpr double kRedLuma = 0.2126;
  constexpr double kGreenLuma = 0.7152;
  constexpr double kBlueLuma = 0.0722;
  return matrix3_from_rows(
      kRedLuma + (1.0 - kRedLuma) * scale,
      kGreenLuma - kGreenLuma * scale,
      kBlueLuma - kBlueLuma * scale,
      kRedLuma - kRedLuma * scale,
      kGreenLuma + (1.0 - kGreenLuma) * scale,
      kBlueLuma - kBlueLuma * scale,
      kRedLuma - kRedLuma * scale,
      kGreenLuma - kGreenLuma * scale,
      kBlueLuma + (1.0 - kBlueLuma) * scale);
}

[[nodiscard]] Matrix3 calibration_hue_matrix(double hue) {
  const auto radians = clamp_signed_percent(hue) / 100.0 * kPi;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  return matrix3_from_rows(
      0.213 + cosine * 0.787 - sine * 0.213,
      0.715 - cosine * 0.715 - sine * 0.715,
      0.072 - cosine * 0.072 + sine * 0.928,
      0.213 - cosine * 0.213 + sine * 0.143,
      0.715 + cosine * 0.285 + sine * 0.140,
      0.072 - cosine * 0.072 - sine * 0.283,
      0.213 - cosine * 0.213 - sine * 0.787,
      0.715 - cosine * 0.715 + sine * 0.715,
      0.072 + cosine * 0.928 + sine * 0.072);
}

[[nodiscard]] Matrix3 calibration_primary_matrix(
    double red_hue,
    double red_saturation,
    double green_hue,
    double green_saturation,
    double blue_hue,
    double blue_saturation) {
  const auto signed_unit = [](double value) {
    return clamp_signed_percent(value) / 100.0;
  };
  const auto red_shift = signed_unit(red_hue) * 0.28;
  const auto green_shift = signed_unit(green_hue) * 0.28;
  const auto blue_shift = signed_unit(blue_hue) * 0.28;
  const auto red_gain = std::max(0.0, 1.0 + signed_unit(red_saturation) * 0.60);
  const auto green_gain = std::max(0.0, 1.0 + signed_unit(green_saturation) * 0.60);
  const auto blue_gain = std::max(0.0, 1.0 + signed_unit(blue_saturation) * 0.60);
  return matrix3_from_rows(
      red_gain,
      std::max(0.0, -green_shift),
      std::max(0.0, blue_shift),
      std::max(0.0, red_shift),
      green_gain,
      std::max(0.0, -blue_shift),
      std::max(0.0, -red_shift),
      std::max(0.0, green_shift),
      blue_gain);
}

[[nodiscard]] Matrix3 calibration_matrix(const InputTransformSettings& settings) {
  return multiply_matrix3(
      calibration_hue_matrix(settings.calibration_hue),
      multiply_matrix3(
          calibration_saturation_matrix(settings.calibration_saturation),
          calibration_primary_matrix(
              settings.red_primary_hue,
              settings.red_primary_saturation,
              settings.green_primary_hue,
              settings.green_primary_saturation,
              settings.blue_primary_hue,
               settings.blue_primary_saturation)));
}

[[nodiscard]] Matrix3 camera_profile_matrix(
    const ColorPipelineConfig& config,
    double source_temperature) {
  if (!config.camera_profile.valid ||
      (config.input_profile.kind != InputTransformKind::CameraDcpProfile &&
       config.input_profile.kind != InputTransformKind::CameraIccProfile)) {
    return identity_matrix3();
  }
  if (config.camera_profile.dual_illuminant) {
    const auto range = std::max(
        1.0,
        config.camera_profile.daylight_temperature - config.camera_profile.tungsten_temperature);
    const auto blend = std::clamp(
        (source_temperature - config.camera_profile.tungsten_temperature) / range,
        0.0,
        1.0);
    Matrix3 matrix;
    for (std::size_t i = 0; i < matrix.values.size(); ++i) {
      matrix.values[i] = config.camera_profile.camera_to_xyz_d50_tungsten[i] * (1.0 - blend) +
                         config.camera_profile.camera_to_xyz_d50_daylight[i] * blend;
    }
    return matrix;
  }
  return Matrix3{.values = config.camera_profile.camera_to_xyz_d50};
}

[[nodiscard]] Matrix3 camera_matching_look_matrix(std::string_view look) {
  if (look == "Neutral") {
    return calibration_saturation_matrix(-12.0);
  }
  if (look == "Vivid") {
    return multiply_matrix3(
        matrix3_from_rows(
            1.04, -0.01, 0.00,
            -0.01, 1.03, -0.01,
            0.00, -0.01, 1.04),
        calibration_saturation_matrix(18.0));
  }
  if (look == "Portrait") {
    return matrix3_from_rows(
        1.04, 0.01, 0.00,
        0.00, 1.00, 0.00,
        -0.01, 0.00, 0.98);
  }
  if (look == "Landscape") {
    return multiply_matrix3(
        matrix3_from_rows(
            1.00, -0.01, 0.00,
            0.00, 1.05, -0.01,
            -0.01, 0.00, 1.06),
        calibration_saturation_matrix(12.0));
  }
  if (look == "Monochrome") {
    constexpr double kRedLuma = 0.2126;
    constexpr double kGreenLuma = 0.7152;
    constexpr double kBlueLuma = 0.0722;
    return matrix3_from_rows(
        kRedLuma, kGreenLuma, kBlueLuma,
        kRedLuma, kGreenLuma, kBlueLuma,
        kRedLuma, kGreenLuma, kBlueLuma);
  }
  return identity_matrix3();
}

[[nodiscard]] std::array<double, 3> multiply_matrix3_vector(
    const Matrix3& matrix,
    const std::array<double, 3>& vector) {
  return {
      matrix.values[0] * vector[0] + matrix.values[1] * vector[1] + matrix.values[2] * vector[2],
      matrix.values[3] * vector[0] + matrix.values[4] * vector[1] + matrix.values[5] * vector[2],
      matrix.values[6] * vector[0] + matrix.values[7] * vector[1] + matrix.values[8] * vector[2],
  };
}

[[nodiscard]] XyzColor multiply_matrix3_xyz(const Matrix3& matrix, const XyzColor& xyz) {
  const auto transformed = multiply_matrix3_vector(matrix, {xyz.x, xyz.y, xyz.z});
  return XyzColor{
      .x = transformed[0],
      .y = transformed[1],
      .z = transformed[2],
  };
}

[[nodiscard]] Matrix3 invert_matrix3(const Matrix3& matrix) {
  const auto& m = matrix.values;
  const auto determinant =
      m[0] * (m[4] * m[8] - m[5] * m[7]) -
      m[1] * (m[3] * m[8] - m[5] * m[6]) +
      m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (std::abs(determinant) <= 1e-12) {
    return identity_matrix3();
  }

  const auto inverse_determinant = 1.0 / determinant;
  return matrix3_from_rows(
      (m[4] * m[8] - m[5] * m[7]) * inverse_determinant,
      (m[2] * m[7] - m[1] * m[8]) * inverse_determinant,
      (m[1] * m[5] - m[2] * m[4]) * inverse_determinant,
      (m[5] * m[6] - m[3] * m[8]) * inverse_determinant,
      (m[0] * m[8] - m[2] * m[6]) * inverse_determinant,
      (m[2] * m[3] - m[0] * m[5]) * inverse_determinant,
      (m[3] * m[7] - m[4] * m[6]) * inverse_determinant,
      (m[1] * m[6] - m[0] * m[7]) * inverse_determinant,
      (m[0] * m[4] - m[1] * m[3]) * inverse_determinant);
}

[[nodiscard]] Chromaticity white_point_chromaticity(WhitePoint white_point) {
  switch (white_point) {
    case WhitePoint::D50:
      return Chromaticity{.x = 0.34567, .y = 0.35850};
    case WhitePoint::D65:
    case WhitePoint::Unknown:
    default:
      return Chromaticity{.x = 0.31271, .y = 0.32902};
  }
}

[[nodiscard]] std::array<Chromaticity, 3> rgb_primaries(ColorPrimaries primaries) {
  switch (primaries) {
    case ColorPrimaries::DisplayP3:
      return {Chromaticity{.x = 0.680, .y = 0.320},
              Chromaticity{.x = 0.265, .y = 0.690},
              Chromaticity{.x = 0.150, .y = 0.060}};
    case ColorPrimaries::AdobeRgb:
      return {Chromaticity{.x = 0.640, .y = 0.330},
              Chromaticity{.x = 0.210, .y = 0.710},
              Chromaticity{.x = 0.150, .y = 0.060}};
    case ColorPrimaries::ProPhotoRgb:
      return {Chromaticity{.x = 0.7347, .y = 0.2653},
              Chromaticity{.x = 0.1596, .y = 0.8404},
              Chromaticity{.x = 0.0366, .y = 0.0001}};
    case ColorPrimaries::Rec2020:
      return {Chromaticity{.x = 0.708, .y = 0.292},
              Chromaticity{.x = 0.170, .y = 0.797},
              Chromaticity{.x = 0.131, .y = 0.046}};
    case ColorPrimaries::Rec709:
    case ColorPrimaries::Srgb:
    case ColorPrimaries::Unknown:
    default:
      return {Chromaticity{.x = 0.640, .y = 0.330},
              Chromaticity{.x = 0.300, .y = 0.600},
              Chromaticity{.x = 0.150, .y = 0.060}};
  }
}

[[nodiscard]] Matrix3 rgb_to_xyz_matrix(const RgbColorSpace& color_space) {
  const auto primaries = rgb_primaries(color_space.primaries);
  const auto red = xyz_from_chromaticity(primaries[0]);
  const auto green = xyz_from_chromaticity(primaries[1]);
  const auto blue = xyz_from_chromaticity(primaries[2]);
  const auto white = xyz_from_chromaticity(white_point_chromaticity(color_space.white_point));

  const auto base = matrix3_from_rows(
      red.x, green.x, blue.x,
      red.y, green.y, blue.y,
      red.z, green.z, blue.z);
  const auto scales = multiply_matrix3_vector(invert_matrix3(base), {white.x, white.y, white.z});
  return matrix3_from_rows(
      red.x * scales[0], green.x * scales[1], blue.x * scales[2],
      red.y * scales[0], green.y * scales[1], blue.y * scales[2],
      red.z * scales[0], green.z * scales[1], blue.z * scales[2]);
}

[[nodiscard]] Matrix3 cone_response_matrix(std::string_view method) {
  if (method == "CAT02") {
    return matrix3_from_rows(
        0.7328, 0.4296, -0.1624,
        -0.7036, 1.6975, 0.0061,
        0.0030, 0.0136, 0.9834);
  }
  return matrix3_from_rows(
      0.8951, 0.2664, -0.1614,
      -0.7502, 1.7135, 0.0367,
      0.0389, -0.0685, 1.0296);
}

[[nodiscard]] Matrix3 chromatic_adaptation_matrix(
    const XyzColor& source_white,
    const XyzColor& target_white,
    std::string_view method) {
  const auto cone = cone_response_matrix(method);
  const auto cone_inverse = invert_matrix3(cone);
  const auto source_cone =
      multiply_matrix3_vector(cone, {source_white.x, source_white.y, source_white.z});
  const auto target_cone =
      multiply_matrix3_vector(cone, {target_white.x, target_white.y, target_white.z});
  const auto diagonal = matrix3_from_rows(
      target_cone[0] / std::max(1e-6, source_cone[0]), 0.0, 0.0,
      0.0, target_cone[1] / std::max(1e-6, source_cone[1]), 0.0,
      0.0, 0.0, target_cone[2] / std::max(1e-6, source_cone[2]));
  return multiply_matrix3(cone_inverse, multiply_matrix3(diagonal, cone));
}

[[nodiscard]] PipelineColorContext make_pipeline_color_context(const ColorAdjustmentState& state) {
  PipelineColorContext context;
  context.working_space = state.color_pipeline.working_profile.color_space;
  context.render_intent = state.color_pipeline.render_intent;
  context.output_gamut_mapping_enabled = state.color_pipeline.output_gamut_mapping_enabled;
  context.output_gamut_mapping_policy = state.color_pipeline.output_gamut_mapping_policy;
  context.output_headroom = std::clamp(state.color_pipeline.output_headroom, 1.0, 16.0);
  context.chromatic_adaptation_method = state.input_transform.chromatic_adaptation_method;
  context.rgb_to_xyz_native = rgb_to_xyz_matrix(context.working_space);
  context.xyz_native_to_rgb = invert_matrix3(context.rgb_to_xyz_native);
  const auto source_white = xyz_from_chromaticity(white_point_chromaticity(context.working_space.white_point));
  const auto d65_white = xyz_from_chromaticity(white_point_chromaticity(WhitePoint::D65));
  const auto to_d65 = chromatic_adaptation_matrix(
      source_white,
      d65_white,
      context.chromatic_adaptation_method);
  const auto from_d65 = chromatic_adaptation_matrix(
      d65_white,
      source_white,
      context.chromatic_adaptation_method);
  context.rgb_to_xyz_d65 = multiply_matrix3(to_d65, context.rgb_to_xyz_native);
  context.xyz_d65_to_rgb = multiply_matrix3(context.xyz_native_to_rgb, from_d65);
  const auto linear_srgb = make_linear_srgb_color_space();
  const auto xyz_d65_to_linear_srgb = invert_matrix3(rgb_to_xyz_matrix(linear_srgb));
  context.rgb_to_linear_srgb = multiply_matrix3(xyz_d65_to_linear_srgb, context.rgb_to_xyz_d65);
  return context;
}

[[nodiscard]] ColorPixel apply_rgb_matrix(const ColorPixel& pixel, const Matrix3& matrix) {
  const auto transformed = multiply_matrix3_vector(
      matrix,
      {static_cast<double>(pixel.red), static_cast<double>(pixel.green), static_cast<double>(pixel.blue)});
  return ColorPixel{
      .red = static_cast<float>(transformed[0]),
      .green = static_cast<float>(transformed[1]),
      .blue = static_cast<float>(transformed[2]),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] XyzColor pixel_to_xyz_native(const ColorPixel& pixel, const PipelineColorContext& context) {
  return multiply_matrix3_xyz(
      context.rgb_to_xyz_native,
      XyzColor{
          .x = std::max(0.0, static_cast<double>(pixel.red)),
          .y = std::max(0.0, static_cast<double>(pixel.green)),
          .z = std::max(0.0, static_cast<double>(pixel.blue)),
      });
}

[[nodiscard]] XyzColor pixel_to_xyz_d65(const ColorPixel& pixel, const PipelineColorContext& context) {
  return multiply_matrix3_xyz(
      context.rgb_to_xyz_d65,
      XyzColor{
          .x = std::max(0.0, static_cast<double>(pixel.red)),
          .y = std::max(0.0, static_cast<double>(pixel.green)),
          .z = std::max(0.0, static_cast<double>(pixel.blue)),
      });
}

[[nodiscard]] ColorPixel pixel_to_linear_srgb_reference(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return apply_rgb_matrix(pixel, context.rgb_to_linear_srgb);
}

[[nodiscard]] double sanitized_black_point(double value, double output_headroom) {
  constexpr auto kBlackPointMargin = 1.0e-6;
  return std::clamp(value, 0.0, std::max(0.0, output_headroom - kBlackPointMargin));
}

[[nodiscard]] bool black_point_compensation_affects_output(
    const ColorPipelineConfig& config) {
  if (!config.black_point_compensation_enabled) {
    return false;
  }

  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto source_black = sanitized_black_point(config.source_black_point, headroom);
  const auto output_black = sanitized_black_point(config.output_black_point, headroom);
  return std::abs(source_black - output_black) > 1.0e-9;
}

[[nodiscard]] ColorPixel apply_black_point_compensation(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config) {
  if (!black_point_compensation_affects_output(config)) {
    return pixel;
  }

  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto source_black = sanitized_black_point(config.source_black_point, headroom);
  const auto output_black = sanitized_black_point(config.output_black_point, headroom);
  const auto source_span = std::max(1.0e-6, headroom - source_black);
  const auto output_span = std::max(0.0, headroom - output_black);
  const auto remap = [&](float channel) {
    const auto value = static_cast<double>(channel);
    if (value <= source_black) {
      return static_cast<float>(output_black);
    }
    const auto normalized = (value - source_black) / source_span;
    return static_cast<float>(std::clamp(
        output_black + normalized * output_span,
        0.0,
        headroom));
  };
  return ColorPixel{
      .red = remap(pixel.red),
      .green = remap(pixel.green),
      .blue = remap(pixel.blue),
      .alpha = pixel.alpha,
  };
}

[[nodiscard, maybe_unused]] bool metal_color_pipeline_supported(const ColorAdjustmentState& state) {
  const auto has_local_white_balance = std::any_of(
      state.local_masks.cbegin(),
      state.local_masks.cend(),
      [](const LocalMask& mask) {
        return std::abs(mask.temperature) > 1.0e-9 || std::abs(mask.tint) > 1.0e-9;
      }) || std::any_of(
      state.local_mask_sets.cbegin(),
      state.local_mask_sets.cend(),
      [](const LocalMaskSet& mask_set) {
        return std::abs(mask_set.temperature) > 1.0e-9 || std::abs(mask_set.tint) > 1.0e-9;
      });

  // The accelerated pre-tone kernel currently has only diagonal RGB white-
  // balance multipliers. Use the CPU path whenever a Bradford/CAT16 matrix is
  // required so preview and export cannot disagree on adapted colors.
  return std::abs(effective_input_temperature(state)) <= 1.0e-9 &&
         std::abs(effective_input_tint(state)) <= 1.0e-9 &&
         std::abs(state.linear_mask_temperature) <= 1.0e-9 &&
         std::abs(state.radial_mask_temperature) <= 1.0e-9 &&
         !has_local_white_balance &&
         state.color_pipeline.working_profile.kind == WorkingTransformKind::LinearSrgb &&
         state.color_pipeline.render_intent == RenderIntent::Perceptual &&
         state.color_pipeline.output_gamut_mapping_enabled &&
         state.color_pipeline.output_gamut_mapping_policy == GamutMappingPolicy::Perceptual &&
         std::abs(state.color_pipeline.output_headroom - 1.0) <= 1e-9 &&
         !black_point_compensation_affects_output(state.color_pipeline);
}

[[nodiscard]] double smoothstep(double edge0, double edge1, double x) {
  if (std::abs(edge1 - edge0) <= 1e-9) {
    return x < edge0 ? 0.0 : 1.0;
  }

  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double bell_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  const auto distance = std::abs(value - center) / radius;
  if (distance >= 1.0) {
    return 0.0;
  }

  const auto t = 1.0 - distance;
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double circular_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto t = 1.0 - distance / radius;
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] double circular_feathered_selection_weight(
    double value,
    double center,
    double radius,
    double softness) {
  if (radius <= 0.0) {
    return 0.0;
  }

  auto distance = std::abs(value - center);
  distance = std::min(distance, 1.0 - distance);
  if (distance >= radius) {
    return 0.0;
  }

  const auto feather = clamp_unit(softness) * radius;
  const auto inner_radius = radius - feather;
  if (distance <= inner_radius || feather <= 1.0e-9) {
    return 1.0;
  }
  return 1.0 - smoothstep(inner_radius, radius, distance);
}

[[nodiscard]] double wrap_unit(double value) {
  value -= std::floor(value);
  if (value < 0.0) {
    value += 1.0;
  }
  return value;
}

[[nodiscard]] double wrap_radians(double value) {
  while (value < 0.0) {
    value += 2.0 * kPi;
  }
  while (value >= 2.0 * kPi) {
    value -= 2.0 * kPi;
  }
  return value;
}

[[nodiscard]] double shortest_angular_delta(double from, double to) {
  auto delta = wrap_radians(to) - wrap_radians(from);
  if (delta > kPi) {
    delta -= 2.0 * kPi;
  } else if (delta < -kPi) {
    delta += 2.0 * kPi;
  }
  return delta;
}

[[nodiscard]] double luminance_for_pixel(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return pixel_to_xyz_native(pixel, context).y;
}

[[nodiscard]] std::string_view normalized_color_mixer_model(const ColorAdjustmentState& state) {
  if (state.color_mixer_model == "HSL" || state.color_mixer_model == "HSV" ||
      state.color_mixer_model == "LCH") {
    return state.color_mixer_model;
  }
  return "OKLab";
}

[[nodiscard]] double rgb_hue_for_pixel(const ColorPixel& pixel) {
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto minimum = std::min({red, green, blue});
  const auto delta = maximum - minimum;
  if (delta <= 1.0e-9) {
    return -1.0;
  }

  double hue = 0.0;
  if (maximum == red) {
    hue = (green - blue) / delta;
    if (hue < 0.0) {
      hue += 6.0;
    }
  } else if (maximum == green) {
    hue = ((blue - red) / delta) + 2.0;
  } else {
    hue = ((red - green) / delta) + 4.0;
  }
  return wrap_unit(hue / 6.0);
}

[[nodiscard]] double mixer_selection_hue(
    const ColorPixel& pixel,
    double oklch_hue,
    double chroma,
    std::string_view model) {
  if (model == "HSL" || model == "HSV") {
    return rgb_hue_for_pixel(pixel);
  }
  if (chroma <= 1.0e-6) {
    return -1.0;
  }
  return wrap_unit(oklch_hue / (2.0 * kPi));
}

void apply_selective_component(
    double amount,
    double target_hue_unit,
    double mixer_weight,
    double* chroma,
    double* hue) {
  if (chroma == nullptr || hue == nullptr) {
    return;
  }
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1.0e-9 || mixer_weight <= 1.0e-9) {
    return;
  }
  const auto current_a = *chroma * std::cos(*hue);
  const auto current_b = *chroma * std::sin(*hue);
  const auto direction = target_hue_unit * 2.0 * kPi + (control < 0.0 ? kPi : 0.0);
  const auto strength = std::abs(control) * mixer_weight * 0.07;
  const auto next_a = current_a + std::cos(direction) * strength;
  const auto next_b = current_b + std::sin(direction) * strength;
  *chroma = std::hypot(next_a, next_b);
  *hue = wrap_radians(std::atan2(next_b, next_a));
}

void apply_selective_color_adjustment(
    const ColorMixerChannelAdjustment& mixer,
    double mixer_weight,
    double* lightness,
    double* chroma,
    double* hue) {
  if (lightness == nullptr || chroma == nullptr || hue == nullptr || mixer_weight <= 1.0e-9) {
    return;
  }

  apply_selective_component(mixer.selective_cyan, 0.50, mixer_weight, chroma, hue);
  apply_selective_component(mixer.selective_magenta, 0.88, mixer_weight, chroma, hue);
  apply_selective_component(mixer.selective_yellow, 0.15, mixer_weight, chroma, hue);
  *lightness = clamp_unit(
      *lightness - mixer_weight * (clamp_signed_percent(mixer.selective_black) / 100.0) * 0.18);
}

[[nodiscard]] bool has_rgb_channel_mixer_adjustments(const ColorAdjustmentState& state) {
  const auto& mixer = state.rgb_channel_mixer;
  return std::abs(mixer.red_from_red - 100.0) > 1e-9 ||
         std::abs(mixer.red_from_green) > 1e-9 ||
         std::abs(mixer.red_from_blue) > 1e-9 ||
         std::abs(mixer.green_from_red) > 1e-9 ||
         std::abs(mixer.green_from_green - 100.0) > 1e-9 ||
         std::abs(mixer.green_from_blue) > 1e-9 ||
         std::abs(mixer.blue_from_red) > 1e-9 ||
         std::abs(mixer.blue_from_green) > 1e-9 ||
         std::abs(mixer.blue_from_blue - 100.0) > 1e-9;
}

[[nodiscard]] ColorPixel apply_rgb_channel_mixer(
    const ColorPixel& pixel,
    const RgbChannelMixerAdjustment& mixer) {
  const auto scale = [](double value) { return value / 100.0; };
  const auto red = static_cast<double>(pixel.red);
  const auto green = static_cast<double>(pixel.green);
  const auto blue = static_cast<double>(pixel.blue);
  return ColorPixel{
      .red = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.red_from_red) + green * scale(mixer.red_from_green) +
              blue * scale(mixer.red_from_blue))),
      .green = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.green_from_red) + green * scale(mixer.green_from_green) +
              blue * scale(mixer.green_from_blue))),
      .blue = static_cast<float>(std::max(
          0.0,
          red * scale(mixer.blue_from_red) + green * scale(mixer.blue_from_green) +
              blue * scale(mixer.blue_from_blue))),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] ColorPixel apply_black_and_white_mixer(
    const ColorPixel& pixel,
    const ColorAdjustmentState& state,
    const PipelineColorContext& context) {
  auto gray = luminance_for_pixel(pixel, context);
  const auto mixer_hue = rgb_hue_for_pixel(pixel);
  if (mixer_hue >= 0.0) {
    for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
      const auto weight = channel_weight_for_selection(
          mixer_hue, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
      if (weight > 0.0) {
        gray *= std::max(
            0.0,
            1.0 + weight * (clamp_signed_percent(state.black_and_white_mixer[static_cast<std::size_t>(ch)]) /
                            100.0) *
                      0.85);
      }
    }
  }
  const auto value = static_cast<float>(std::max(0.0, gray));
  return ColorPixel{.red = value, .green = value, .blue = value, .alpha = pixel.alpha};
}

[[nodiscard]] bool has_per_channel_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::any_of(
      state.color_mixer_channels.begin(), state.color_mixer_channels.end(),
      [](const ColorMixerChannelAdjustment& channel) {
        return std::abs(channel.hue) > 1e-9 || std::abs(channel.saturation) > 1e-9 ||
               std::abs(channel.luminance) > 1e-9 ||
               std::abs(channel.selective_cyan) > 1e-9 ||
               std::abs(channel.selective_magenta) > 1e-9 ||
               std::abs(channel.selective_yellow) > 1e-9 ||
               std::abs(channel.selective_black) > 1e-9;
      });
}

[[nodiscard]] bool has_color_grading_adjustments(const ColorAdjustmentState& state) {
  return clamp_unit(state.grading_amount / 100.0) > 1e-9 &&
         (std::abs(state.grading_shadows_hue) > 1e-9 ||
         std::abs(state.grading_shadows_saturation) > 1e-9 ||
         std::abs(state.grading_shadows_luminance) > 1e-9 ||
         std::abs(state.grading_midtones_hue) > 1e-9 ||
         std::abs(state.grading_midtones_saturation) > 1e-9 ||
         std::abs(state.grading_midtones_luminance) > 1e-9 ||
         std::abs(state.grading_highlights_hue) > 1e-9 ||
         std::abs(state.grading_highlights_saturation) > 1e-9 ||
         std::abs(state.grading_highlights_luminance) > 1e-9);
}

[[nodiscard]] bool has_color_replacement_adjustments(const ColorAdjustmentState& state) {
  return state.color_replacement_enabled && std::abs(state.color_replacement_strength) > 1e-9;
}

[[nodiscard]] double skin_tone_protection_weight(
    const ColorAdjustmentState& state,
    double normalized_hue,
    double chroma) {
  if (!state.skin_tone_protection_enabled || std::abs(state.skin_tone_protection_strength) <= 1e-9) {
    return 0.0;
  }
  const auto range = std::clamp(state.skin_tone_protection_range / 100.0, 0.01, 1.0) * 0.50;
  return circular_weight(normalized_hue, wrap_unit(state.skin_tone_line_hue), range) *
         smoothstep(0.02, 0.12, chroma) *
         clamp_unit(state.skin_tone_protection_strength / 100.0);
}

[[nodiscard]] double skin_tone_protection_scale(
    const ColorAdjustmentState& state,
    double normalized_hue,
    double chroma) {
  return 1.0 - 0.95 * skin_tone_protection_weight(state, normalized_hue, chroma);
}

[[nodiscard]] double default_skin_chroma_protection(double normalized_hue, double chroma) {
  return 1.0 - 0.38 * circular_weight(normalized_hue, 0.08, 0.15) *
                   smoothstep(0.018, 0.13, chroma);
}

[[nodiscard]] double tone_chroma_protection(double lightness) {
  const auto shadow_protection = smoothstep(0.05, 0.20, lightness);
  const auto highlight_protection = 1.0 - smoothstep(0.78, 0.98, lightness);
  return std::clamp(shadow_protection * highlight_protection, 0.0, 1.0);
}

[[nodiscard]] double highlight_chroma_compression_scale(double lightness, double chroma) {
  const auto highlight_weight = smoothstep(0.78, 0.98, lightness);
  const auto chroma_weight = smoothstep(0.035, 0.22, chroma);
  return std::clamp(1.0 - 0.42 * highlight_weight * chroma_weight, 0.52, 1.0);
}

[[nodiscard]] bool highlight_chroma_compression_enabled(
    const ColorAdjustmentState& state,
    const ColorPixel& pixel,
    double output_headroom) {
  const auto peak = std::max({pixel.red, pixel.green, pixel.blue});
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  if (static_cast<double>(peak) > headroom + 1.0e-5) {
    return true;
  }
  if (peak < 0.98F) {
    return false;
  }
  return std::abs(effective_input_exposure(state)) > 1.0e-9 ||
         std::abs(state.filmic_shoulder) > 1.0e-9 ||
         std::abs(state.hdr_local_tone_mapping) > 1.0e-9;
}

[[nodiscard]] bool has_color_harmony_adjustments(const ColorAdjustmentState& state) {
  return state.color_harmony_enabled && std::abs(state.color_harmony_strength) > 1e-9;
}

[[nodiscard]] std::array<double, 4> color_harmony_anchors(const ColorAdjustmentState& state, int* count) {
  const auto base = wrap_unit(state.color_harmony_base_hue);
  if (state.color_harmony_mode == "Analogous") {
    *count = 3;
    return {wrap_unit(base - 0.08333333333333333), base, wrap_unit(base + 0.08333333333333333), base};
  }
  if (state.color_harmony_mode == "Triadic") {
    *count = 3;
    return {base, wrap_unit(base + 1.0 / 3.0), wrap_unit(base + 2.0 / 3.0), base};
  }
  if (state.color_harmony_mode == "Split Complementary") {
    *count = 3;
    return {base, wrap_unit(base + 5.0 / 12.0), wrap_unit(base + 7.0 / 12.0), base};
  }
  if (state.color_harmony_mode == "Tetradic") {
    *count = 4;
    return {base, wrap_unit(base + 0.25), wrap_unit(base + 0.50), wrap_unit(base + 0.75)};
  }
  *count = 2;
  return {base, wrap_unit(base + 0.50), base, base};
}

[[nodiscard]] double nearest_color_harmony_hue(const ColorAdjustmentState& state, double normalized_hue) {
  auto count = 0;
  const auto anchors = color_harmony_anchors(state, &count);
  auto best_hue = anchors[0];
  auto best_distance = 1.0;
  for (int i = 0; i < count; ++i) {
    auto distance = std::abs(normalized_hue - anchors[static_cast<std::size_t>(i)]);
    distance = std::min(distance, 1.0 - distance);
    if (distance < best_distance) {
      best_distance = distance;
      best_hue = anchors[static_cast<std::size_t>(i)];
    }
  }
  return best_hue;
}

[[nodiscard]] bool has_legacy_color_mixer_adjustments(const ColorAdjustmentState& state) {
  return std::abs(state.hue_adjustment) > 1e-9 ||
         std::abs(state.saturation_adjustment) > 1e-9 ||
         std::abs(state.luminance_adjustment) > 1e-9;
}

[[nodiscard]] bool has_post_tone_color_adjustments(
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength) {
  return std::abs(saturation_strength) > 1e-9 ||
         std::abs(vibrance_strength) > 1e-9 ||
          has_per_channel_color_mixer_adjustments(state) ||
          has_legacy_color_mixer_adjustments(state) ||
          has_color_replacement_adjustments(state) ||
          has_color_harmony_adjustments(state) ||
          has_color_grading_adjustments(state);
}

[[nodiscard]] ColorPixel scale_pixel_to_luminance(
    const ColorPixel& pixel,
    double target_luminance,
    const PipelineColorContext& context) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto current_luminance = luminance_for_pixel(pixel, context);
  const auto max_input_channel = std::max({pixel.red, pixel.green, pixel.blue});
  if (max_input_channel <= 1e-9) {
    return ColorPixel{
        .red = static_cast<float>(clamped_target),
        .green = static_cast<float>(clamped_target),
        .blue = static_cast<float>(clamped_target),
        .alpha = pixel.alpha,
    };
  }

  const auto safe_luminance = std::max(current_luminance, 1e-6);
  const auto scale = clamped_target / safe_luminance;
  return ColorPixel{
      .red = static_cast<float>(std::max(0.0, pixel.red * scale)),
      .green = static_cast<float>(std::max(0.0, pixel.green * scale)),
      .blue = static_cast<float>(std::max(0.0, pixel.blue * scale)),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] double headroom_safe_luminance_target(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  const auto max_input_channel = std::max({
      static_cast<double>(pixel.red),
      static_cast<double>(pixel.green),
      static_cast<double>(pixel.blue),
  });
  const auto current_luminance = luminance_for_pixel(pixel, context);
  if (max_input_channel <= 1.0e-9 || current_luminance <= 1.0e-9) {
    return std::min(clamped_target, headroom);
  }

  // For chroma-preserving tone, p' = p * (Y' / Y). The peak-channel
  // constraint max(p') <= H therefore requires Y' <= H * Y / max(p).
  const auto safe_target = headroom * current_luminance / max_input_channel;
  return std::min(clamped_target, std::max(0.0, safe_target));
}

[[nodiscard]] ColorPixel scale_pixel_to_luminance_with_headroom(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context) {
  return scale_pixel_to_luminance(
      pixel,
      headroom_safe_luminance_target(pixel, target_luminance, output_headroom, context),
      context);
}

[[nodiscard]] ColorPixel fit_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  if (static_cast<double>(pixel.red) >= 0.0 &&
      static_cast<double>(pixel.green) >= 0.0 &&
      static_cast<double>(pixel.blue) >= 0.0 &&
      static_cast<double>(pixel.red) <= headroom &&
      static_cast<double>(pixel.green) <= headroom &&
      static_cast<double>(pixel.blue) <= headroom) {
    return pixel;
  }
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto scale = maximum > headroom ? headroom / maximum : 1.0;
  return ColorPixel{
      .red = static_cast<float>(red * scale),
      .green = static_cast<float>(green * scale),
      .blue = static_cast<float>(blue * scale),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] ColorPixel clip_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  return ColorPixel{
      .red = static_cast<float>(std::clamp(static_cast<double>(pixel.red), 0.0, headroom)),
      .green = static_cast<float>(std::clamp(static_cast<double>(pixel.green), 0.0, headroom)),
      .blue = static_cast<float>(std::clamp(static_cast<double>(pixel.blue), 0.0, headroom)),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] std::array<std::pair<double, double>, 6> tone_curve_control_points(const ColorAdjustmentState& state) {
  const auto point = [](double x, double delta) {
    return std::pair<double, double>{x, clamp_unit(x + (delta / 100.0) * 0.34)};
  };

  return {
      std::pair<double, double>{0.0, 0.0},
      point(0.20, state.curve_shadows),
      point(0.40, state.curve_darks),
      point(0.65, state.curve_lights),
      point(0.85, state.curve_highlights),
      std::pair<double, double>{1.0, 1.0},
  };
}

[[nodiscard]] bool has_point_curve(std::span<const CurvePoint> points) {
  return points.size() >= 2;
}

[[nodiscard]] CurveEvaluationData build_curve_data(std::span<const CurvePoint> input_points);

[[nodiscard]] CurveEvaluationData build_parametric_curve_data(const ColorAdjustmentState& state) {
  CurveEvaluationData data;
  const auto points = tone_curve_control_points(state);
  std::vector<CurvePoint> control_points;
  control_points.reserve(points.size());
  for (const auto& [x, y] : points) {
    control_points.push_back(CurvePoint{.x = x, .y = y});
  }
  return build_curve_data(control_points);
}

[[nodiscard]] CurveEvaluationData build_curve_data(std::span<const CurvePoint> input_points) {
  CurveEvaluationData data;
  if (input_points.size() < 2) {
    return data;
  }

  data.points.assign(input_points.begin(), input_points.end());
  std::sort(data.points.begin(), data.points.end(), [](const CurvePoint& left, const CurvePoint& right) {
    return left.x < right.x;
  });

  std::vector<CurvePoint> normalized_points;
  normalized_points.reserve(data.points.size());
  for (const auto& point : data.points) {
    const auto x = clamp_unit(point.x);
    const auto y = clamp_unit(point.y);
    if (!normalized_points.empty() && std::abs(normalized_points.back().x - x) <= 1e-6) {
      normalized_points.back().y = y;
      continue;
    }
    normalized_points.push_back(CurvePoint{.x = x, .y = y});
  }

  if (normalized_points.size() < 2) {
    return {};
  }

  normalized_points.front().x = 0.0;
  normalized_points.back().x = 1.0;
  data.points = std::move(normalized_points);

  if (data.points.size() == 2) {
    const auto span = std::max(1e-6, data.points.back().x - data.points.front().x);
    const auto slope = (data.points.back().y - data.points.front().y) / span;
    data.tangents = {slope, slope};
    return data;
  }

  const auto segment_count = data.points.size() - 1;
  std::vector<double> secants(segment_count, 0.0);
  for (std::size_t index = 0; index < segment_count; ++index) {
    const auto span = std::max(1e-6, data.points[index + 1].x - data.points[index].x);
    secants[index] = (data.points[index + 1].y - data.points[index].y) / span;
  }

  data.tangents.resize(data.points.size(), 0.0);
  data.tangents.front() = secants.front();
  data.tangents.back() = secants.back();
  for (std::size_t index = 1; index + 1 < data.points.size(); ++index) {
    if (secants[index - 1] * secants[index] <= 0.0) {
      data.tangents[index] = 0.0;
    } else {
      data.tangents[index] = 0.5 * (secants[index - 1] + secants[index]);
    }
  }

  for (std::size_t index = 0; index < segment_count; ++index) {
    if (std::abs(secants[index]) <= 1e-9) {
      data.tangents[index] = 0.0;
      data.tangents[index + 1] = 0.0;
      continue;
    }

    const auto a = data.tangents[index] / secants[index];
    const auto b = data.tangents[index + 1] / secants[index];
    const auto magnitude = std::hypot(a, b);
    if (magnitude <= 3.0) {
      continue;
    }

    const auto scale = 3.0 / magnitude;
    data.tangents[index] = scale * a * secants[index];
    data.tangents[index + 1] = scale * b * secants[index];
  }

  return data;
}

[[nodiscard]] double evaluate_curve_data(double value, const CurveEvaluationData& data) {
  if (data.points.size() < 2) {
    return clamp_unit(value);
  }

  const auto clamped_value = clamp_unit(value);
  for (std::size_t index = 0; index + 1 < data.points.size(); ++index) {
    const auto& start = data.points[index];
    const auto& end = data.points[index + 1];
    if (clamped_value > end.x && index + 2 <= data.points.size()) {
      continue;
    }

    const auto span = std::max(1e-6, end.x - start.x);
    const auto t = clamp_unit((clamped_value - start.x) / span);
    const auto t2 = t * t;
    const auto t3 = t2 * t;
    const auto h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const auto h10 = t3 - 2.0 * t2 + t;
    const auto h01 = -2.0 * t3 + 3.0 * t2;
    const auto h11 = t3 - t2;
    const auto result = h00 * start.y + h10 * span * data.tangents[index] +
                        h01 * end.y + h11 * span * data.tangents[index + 1];
    return clamp_unit(result);
  }

  return clamp_unit(clamped_value);
}

[[nodiscard]] double zone_exposure_delta(double stops, const ColorAdjustmentState& state);

struct ToneAnchor final {
  double input_stops = 0.0;
  double output_stops = 0.0;
};

[[nodiscard]] double gentle_signed_response(double control) {
  const auto clamped = std::clamp(control, -1.0, 1.0);
  return std::copysign(std::pow(std::abs(clamped), 1.18), clamped);
}

[[nodiscard]] double interpolate_tone_anchors(
    double stops,
    std::span<const ToneAnchor> anchors) {
  if (anchors.empty()) {
    return stops;
  }
  if (stops <= anchors.front().input_stops) {
    return stops + (anchors.front().output_stops - anchors.front().input_stops);
  }
  if (stops >= anchors.back().input_stops) {
    return stops + (anchors.back().output_stops - anchors.back().input_stops) * 0.72;
  }

  for (std::size_t index = 0; index + 1 < anchors.size(); ++index) {
    const auto& left = anchors[index];
    const auto& right = anchors[index + 1];
    if (stops < left.input_stops || stops > right.input_stops) {
      continue;
    }
    const auto span = std::max(1.0e-6, right.input_stops - left.input_stops);
    const auto t = clamp_unit((stops - left.input_stops) / span);
    const auto eased = t * t * (3.0 - 2.0 * t);
    const auto left_delta = left.output_stops - left.input_stops;
    const auto right_delta = right.output_stops - right.input_stops;
    return stops + std::lerp(left_delta, right_delta, eased);
  }

  return stops;
}

[[nodiscard]] double apply_photographic_tone_curve(double value, const ColorAdjustmentState& state) {
  if (std::abs(state.contrast) <= 1.0e-9 &&
      std::abs(state.highlights) <= 1.0e-9 &&
      std::abs(state.shadows) <= 1.0e-9 &&
      std::abs(state.whites) <= 1.0e-9 &&
      std::abs(state.blacks) <= 1.0e-9 &&
      std::abs(state.brightness) <= 1.0e-9 &&
      std::abs(state.zone_deep_shadows) <= 1.0e-9 &&
      std::abs(state.zone_shadows) <= 1.0e-9 &&
      std::abs(state.zone_midtones) <= 1.0e-9 &&
      std::abs(state.zone_lights) <= 1.0e-9 &&
      std::abs(state.zone_highlights) <= 1.0e-9) {
    return value;
  }
  const auto safe_value = std::max(1.0e-8, value);
  const auto input_stops = std::log2(safe_value / kMiddleGray);
  const auto contrast = gentle_signed_response(clamp_signed_percent(state.contrast) / 100.0);
  const auto highlights = gentle_signed_response(clamp_signed_percent(state.highlights) / 100.0);
  const auto shadows = gentle_signed_response(clamp_signed_percent(state.shadows) / 100.0);
  const auto whites = gentle_signed_response(clamp_signed_percent(state.whites) / 100.0);
  const auto blacks = gentle_signed_response(clamp_signed_percent(state.blacks) / 100.0);
  const auto brightness = gentle_signed_response(clamp_signed_percent(state.brightness) / 100.0);

  constexpr std::array<double, 9> kInputStops = {
      -8.0, -5.25, -3.20, -1.45, 0.0, 1.35, 2.55, 4.25, 6.50};
  constexpr std::array<double, 9> kContrastWeights = {
      0.02, 0.08, 0.34, 0.82, 0.0, 0.86, 0.46, 0.10, 0.03};
  std::array<ToneAnchor, 9> anchors{};
  const auto contrast_scale = contrast >= 0.0 ? 0.44 : 0.58;
  for (std::size_t index = 0; index < anchors.size(); ++index) {
    const auto local_slope = std::clamp(
        1.0 + contrast * contrast_scale * kContrastWeights[index],
        0.56,
        1.42);
    anchors[index] = ToneAnchor{
        .input_stops = kInputStops[index],
        .output_stops = kInputStops[index] * local_slope,
    };
  }

  anchors[0].output_stops += blacks * 2.80;
  anchors[1].output_stops += blacks * 2.60;
  anchors[2].output_stops += blacks * 3.60 + shadows * 0.42;
  anchors[3].output_stops += shadows * 1.14 + blacks * 0.30;
  anchors[4].output_stops += brightness * 0.90;
  anchors[5].output_stops += brightness * 0.65 + highlights * 0.68;
  anchors[6].output_stops += highlights * 1.04 + whites * 0.18;
  anchors[7].output_stops += whites * 0.82 + highlights * 0.16;
  anchors[8].output_stops += whites * 1.12;

  const auto zone_offset =
      zone_exposure_delta(input_stops, state) * 0.75;
  anchors[1].output_stops += zone_offset * 0.40;
  anchors[2].output_stops += zone_offset * 0.68;
  anchors[3].output_stops += zone_offset * 0.82;
  anchors[4].output_stops += zone_offset * 0.74;
  anchors[5].output_stops += zone_offset * 0.68;
  anchors[6].output_stops += zone_offset * 0.58;
  anchors[7].output_stops += zone_offset * 0.34;

  constexpr auto kMinimumStopSeparation = 0.18;
  for (std::size_t index = 1; index < anchors.size(); ++index) {
    anchors[index].output_stops =
        std::max(anchors[index].output_stops, anchors[index - 1].output_stops + kMinimumStopSeparation);
  }

  return kMiddleGray * std::pow(2.0, interpolate_tone_anchors(input_stops, anchors));
}

[[nodiscard]] double apply_filmic_toe(double value, double amount) {
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1e-6) {
    return value;
  }
  const auto safe_value = std::max(0.0, value);
  const auto shadow_weight = 1.0 - smoothstep(kMiddleGray * 0.35, kMiddleGray * 2.0, safe_value);
  if (shadow_weight <= 1e-6) {
    return safe_value;
  }
  if (control > 0.0) {
    const auto lifted = std::sqrt(safe_value * kMiddleGray);
    return std::lerp(safe_value, lifted, control * shadow_weight * 0.72);
  }
  const auto crushed = safe_value * (1.0 - (-control) * 0.58 * shadow_weight);
  return std::max(0.0, crushed);
}

[[nodiscard]] double apply_adaptive_contrast(double value, double amount) {
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1e-6) {
    return value;
  }
  const auto safe_value = std::max(1e-6, value);
  const auto stops = std::log2(safe_value / kMiddleGray);
  const auto midtone_weight = 1.0 - smoothstep(0.85, 3.35, std::abs(stops));
  if (midtone_weight <= 1e-6) {
    return value;
  }
  const auto slope = std::clamp(1.0 + control * 0.42 * midtone_weight, 0.58, 1.48);
  const auto contrasted = kMiddleGray * std::pow(safe_value / kMiddleGray, slope);
  const auto detail_weight = std::clamp(midtone_weight * (0.42 + std::abs(control) * 0.58), 0.0, 1.0);
  return std::lerp(safe_value, contrasted, detail_weight);
}

[[nodiscard]] double apply_levels(double value, const ColorAdjustmentState& state) {
  const auto black_control = clamp_unit(state.levels_black_point / 100.0);
  const auto white_control = clamp_unit(state.levels_white_point / 100.0);
  const auto gamma_control = clamp_signed_percent(state.levels_gamma) / 100.0;
  if (black_control <= 1e-6 && white_control <= 1e-6 && std::abs(gamma_control) <= 1e-6) {
    return value;
  }

  const auto input_black = black_control * 0.95;
  const auto input_white = std::max(input_black + 0.01, 1.0 - white_control * 0.95);
  const auto normalized = std::max(0.0, (value - input_black) / (input_white - input_black));
  const auto gamma = std::pow(2.0, -gamma_control * 1.45);
  return std::pow(normalized, gamma);
}

[[nodiscard]] double zone_exposure_delta(double stops, const ColorAdjustmentState& state) {
  auto delta = 0.0;
  delta += bell_weight(stops, -3.50, 1.45) * (clamp_signed_percent(state.zone_deep_shadows) / 100.0) * 0.95;
  delta += bell_weight(stops, -1.70, 1.35) * (clamp_signed_percent(state.zone_shadows) / 100.0) * 0.90;
  delta += bell_weight(stops, 0.00, 1.50) * (clamp_signed_percent(state.zone_midtones) / 100.0) * 0.82;
  delta += bell_weight(stops, 1.70, 1.35) * (clamp_signed_percent(state.zone_lights) / 100.0) * 0.86;
  delta += bell_weight(stops, 3.30, 1.55) * (clamp_signed_percent(state.zone_highlights) / 100.0) * 0.90;
  return delta;
}

[[nodiscard]] double apply_zone_exposure(
    double adjusted_value,
    double source_value,
    const ColorAdjustmentState& state) {
  if (std::abs(state.zone_deep_shadows) <= 1.0e-9 &&
      std::abs(state.zone_shadows) <= 1.0e-9 &&
      std::abs(state.zone_midtones) <= 1.0e-9 &&
      std::abs(state.zone_lights) <= 1.0e-9 &&
      std::abs(state.zone_highlights) <= 1.0e-9) {
    return adjusted_value;
  }

  const auto safe_value = std::max(1.0e-8, source_value);
  const auto stops = std::log2(safe_value / kMiddleGray);
  const auto delta = zone_exposure_delta(stops, state) * 1.15;
  return std::max(0.0, adjusted_value * std::pow(2.0, delta));
}

[[nodiscard]] double apply_global_tone(double value, const ColorAdjustmentState& state) {
  const auto safe_value = std::max(0.0, value);
  auto adjusted = apply_photographic_tone_curve(safe_value, state);
  adjusted = apply_zone_exposure(adjusted, safe_value, state);
  adjusted = apply_adaptive_contrast(adjusted, state.adaptive_contrast);
  adjusted = apply_filmic_toe(adjusted, state.filmic_toe);
  adjusted = apply_levels(adjusted, state);

  return adjusted;
}

[[nodiscard]] bool post_local_tone_pipeline_is_identity(const ColorAdjustmentState& state) {
  return std::abs(state.contrast) <= 1e-9 &&
         std::abs(state.highlights) <= 1e-9 &&
         std::abs(state.shadows) <= 1e-9 &&
         std::abs(state.whites) <= 1e-9 &&
         std::abs(state.blacks) <= 1e-9 &&
         std::abs(state.brightness) <= 1e-9 &&
         std::abs(state.hdr_local_tone_mapping) <= 1e-9 &&
         std::abs(state.filmic_shoulder) <= 1e-9 &&
         std::abs(state.filmic_toe) <= 1e-9 &&
         std::abs(state.adaptive_contrast) <= 1e-9 &&
         std::abs(state.levels_black_point) <= 1e-9 &&
         std::abs(state.levels_gamma) <= 1e-9 &&
         std::abs(state.levels_white_point) <= 1e-9 &&
         std::abs(state.zone_deep_shadows) <= 1e-9 &&
         std::abs(state.zone_shadows) <= 1e-9 &&
         std::abs(state.zone_midtones) <= 1e-9 &&
         std::abs(state.zone_lights) <= 1e-9 &&
         std::abs(state.zone_highlights) <= 1e-9 &&
         std::abs(state.curve_shadows) <= 1e-9 &&
         std::abs(state.curve_darks) <= 1e-9 &&
         std::abs(state.curve_lights) <= 1e-9 &&
         std::abs(state.curve_highlights) <= 1e-9 &&
         state.master_curve_points.empty() &&
         state.luma_curve_points.empty() &&
         state.red_curve_points.empty() &&
         state.green_curve_points.empty() &&
         state.blue_curve_points.empty();
}

[[nodiscard]] double sanitized_output_headroom(const ColorAdjustmentState& state) {
  return std::clamp(state.color_pipeline.output_headroom, 1.0, 16.0);
}

[[nodiscard]] double shoulder_threshold_for_max_luminance(
    double max_luminance,
    double output_headroom,
    double filmic_shoulder = 0.0) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  if (max_luminance <= headroom) {
    return headroom;
  }

  const auto overshoot = std::max(0.0, max_luminance - headroom);
  const auto strength = overshoot / (overshoot + headroom);
  const auto control = clamp_signed_percent(filmic_shoulder) / 100.0;
  const auto control_shift = control >= 0.0 ? control * 0.18 : control * 0.12;
  return headroom * std::clamp(0.98 - strength * 0.18 - control_shift, 0.64, 0.995);
}

[[nodiscard]] double apply_highlight_shoulder(
    double value,
    double threshold,
    double output_headroom) {
  if (value <= threshold) {
    return value;
  }

  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  const auto bounded_threshold = std::min(threshold, headroom);
  const auto shoulder_scale = std::max(1e-6, headroom - bounded_threshold);
  return bounded_threshold +
         shoulder_scale * (1.0 - std::exp(-(value - bounded_threshold) / shoulder_scale));
}

[[nodiscard]] double evaluate_tone_curve_target(
    double target_luminance,
    const CurveEvaluationData& curve,
    bool explicit_point_curve,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  if (target_luminance > 1.0 && !explicit_point_curve) {
    return std::clamp(target_luminance, 0.0, headroom);
  }
  return explicit_point_curve ? clamp_unit(target_luminance)
                              : evaluate_curve_data(clamp_unit(target_luminance), curve);
}

[[nodiscard]] bool has_extended_range_rgb(const ColorPixel& pixel) {
  constexpr auto kTolerance = 1.0e-6F;
  return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
         pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
         pixel.green < -kTolerance || pixel.blue < -kTolerance;
}

[[nodiscard]] Matrix3 white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context) {
  const auto temperature_to_kelvin = [](double slider_value) {
    const auto clamped = clamp_signed_percent(slider_value);
    if (clamped >= 0.0) {
      return std::lerp(5500.0, 2000.0, clamped / 100.0);
    }
    return std::lerp(5500.0, 12000.0, -clamped / 100.0);
  };

  const auto chromaticity_from_kelvin = [](double kelvin) {
    const auto safe_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
    double x = 0.3127;
    double y = 0.3290;
    if (safe_kelvin < 4000.0) {
      x = -0.2661239e9 / std::pow(safe_kelvin, 3.0) -
          0.2343589e6 / std::pow(safe_kelvin, 2.0) +
          0.8776956e3 / safe_kelvin + 0.179910;
      if (safe_kelvin < 2222.0) {
        y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
      } else {
        y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
      }
    } else {
      if (safe_kelvin <= 7000.0) {
        x = -4.6070e9 / std::pow(safe_kelvin, 3.0) +
            2.9678e6 / std::pow(safe_kelvin, 2.0) +
            0.09911e3 / safe_kelvin + 0.244063;
      } else {
        x = -2.0064e9 / std::pow(safe_kelvin, 3.0) +
            1.9018e6 / std::pow(safe_kelvin, 2.0) +
            0.24748e3 / safe_kelvin + 0.237040;
      }
      y = -3.0 * x * x + 2.87 * x - 0.275;
    }
    return std::array<double, 2>{x, y};
  };

  const auto white_xyz_from_kelvin = [&](double kelvin, double tint_value) {
    auto chromaticity = chromaticity_from_kelvin(kelvin);
    auto xyz = xyz_from_chromaticity(Chromaticity{.x = chromaticity[0], .y = chromaticity[1]});
    const auto tint_strength = clamp_signed_percent(tint_value) / 100.0;
    if (std::abs(tint_strength) <= 1e-9) {
      return xyz;
    }
    const auto cone = cone_response_matrix(context.chromatic_adaptation_method);
    const auto cone_inverse = invert_matrix3(cone);
    auto response = multiply_matrix3_vector(cone, {xyz.x, xyz.y, xyz.z});
    response[0] *= std::pow(2.0, tint_strength * 0.08);
    response[1] *= std::pow(2.0, -tint_strength * 0.24);
    response[2] *= std::pow(2.0, tint_strength * 0.08);
    xyz = multiply_matrix3_xyz(
        cone_inverse,
        XyzColor{.x = response[0], .y = response[1], .z = response[2]});
    const auto normalization = std::max(1e-6, xyz.y);
    xyz.x /= normalization;
    xyz.y /= normalization;
    xyz.z /= normalization;
    return xyz;
  };

  if (std::abs(temperature) <= 1e-9 && std::abs(tint) <= 1e-9) {
    return identity_matrix3();
  }

  const auto reference_white = white_xyz_from_kelvin(5500.0, 0.0);
  const auto target_white = white_xyz_from_kelvin(temperature_to_kelvin(temperature), tint);
  auto matrix = multiply_matrix3(
      context.xyz_native_to_rgb,
      multiply_matrix3(
          chromatic_adaptation_matrix(
              reference_white,
              target_white,
              context.chromatic_adaptation_method),
          context.rgb_to_xyz_native));
  const auto transformed_neutral = apply_rgb_matrix(
      ColorPixel{.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
      matrix);
  const auto normalization = luminance_for_pixel(transformed_neutral, context);
  if (normalization <= 1e-6) {
    return identity_matrix3();
  }
  const auto inverse_normalization = 1.0 / normalization;
  for (auto& value : matrix.values) {
    value *= inverse_normalization;
  }
  return matrix;
}

[[nodiscard]] Matrix3 white_balance_matrix_for_kelvin(
    double kelvin,
    double tint,
    const PipelineColorContext& context) {
  const auto clamped_kelvin = std::clamp(kelvin, 2000.0, 12000.0);
  const auto slider = clamped_kelvin >= 5500.0
                          ? -((clamped_kelvin - 5500.0) / 6500.0) * 100.0
                          : ((5500.0 - clamped_kelvin) / 3500.0) * 100.0;
  return white_balance_matrix(slider, tint, context);
}

[[nodiscard]] Matrix3 white_balance_matrix_between_whites(
    double source_kelvin,
    double source_tint,
    double target_kelvin,
    double target_tint,
    const PipelineColorContext& context) {
  const auto source = white_balance_matrix_for_kelvin(source_kelvin, source_tint, context);
  const auto target = white_balance_matrix_for_kelvin(target_kelvin, target_tint, context);
  return multiply_matrix3(target, invert_matrix3(source));
}

[[nodiscard]] double highlight_reconstruction_strength(const ColorAdjustmentState& state) {
  if (!state.input_transform.raw_input_transform_enabled ||
      !state.input_transform.highlight_recovery_enabled) {
    return 0.0;
  }

  const auto mode = state.input_transform.highlight_reconstruction_mode;
  if (mode == "Disabled") {
    return 0.0;
  }
  if (mode == "Camera Default") {
    return 0.35;
  }
  return 0.70;
}

[[nodiscard]] ColorPixel apply_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const PipelineColorContext& context) {
  if (strength <= 1e-6) {
    return input;
  }

  const auto peak = std::max({static_cast<double>(input.red),
                              static_cast<double>(input.green),
                              static_cast<double>(input.blue)});
  if (peak <= 1.0 + 1e-6) {
    return input;
  }

  const auto original_luminance = luminance_for_pixel(input, context);
  const auto blend = clamp_unit(smoothstep(1.0, 4.0, peak) * strength);
  auto reconstructed = input;
  auto reconstruct_channel = [&](float channel) {
    const auto current = static_cast<double>(channel);
    const auto deficit = std::max(0.0, peak - current);
    const auto lift = blend * smoothstep(peak * 0.05, peak * 0.85, deficit);
    return static_cast<float>(current + deficit * lift);
  };
  reconstructed.red = reconstruct_channel(reconstructed.red);
  reconstructed.green = reconstruct_channel(reconstructed.green);
  reconstructed.blue = reconstruct_channel(reconstructed.blue);
  return scale_pixel_to_luminance(reconstructed, original_luminance, context);
}

[[nodiscard]] InputStageParameters make_input_stage_parameters(
    const ColorAdjustmentState& state,
    const PipelineColorContext& context) {
  InputStageParameters parameters;
  const auto input_transform_enabled = state.input_transform.raw_input_transform_enabled;
  const auto& recipe = state.color_pipeline.input_transform_recipe;
  const auto has_source_white_balance =
      input_transform_enabled && recipe.has_source_white_balance;
  const auto source_temperature =
      has_source_white_balance ? recipe.source_temperature : 5500.0;
  const auto source_tint =
      has_source_white_balance ? recipe.source_tint : 0.0;
  parameters.exposure_gain = std::pow(
      2.0,
      (input_transform_enabled ? recipe.baseline_exposure : 0.0) +
          effective_input_exposure(state));
  if (input_transform_enabled) {
    parameters.camera_profile = camera_profile_matrix(state.color_pipeline, source_temperature);
    parameters.camera_matching_look =
        camera_matching_look_matrix(state.input_transform.camera_matching_look);
    parameters.calibration = calibration_matrix(state.input_transform);
  }
  parameters.highlight_reconstruction_strength =
      highlight_reconstruction_strength(state) *
      std::clamp(1.15 - recipe.highlight_headroom, 0.35, 1.0);
  if (input_transform_enabled) {
    parameters.raw_black_level = std::clamp(state.input_transform.raw_black_level, 0.0, 0.95);
    parameters.raw_white_level = std::clamp(
        state.input_transform.raw_white_level,
        parameters.raw_black_level + 0.01,
        4.0);
  }

  if (input_transform_enabled &&
      state.input_transform.raw_white_balance_override_enabled) {
    parameters.white_balance = white_balance_matrix_between_whites(
        source_temperature,
        source_tint,
        state.input_transform.raw_white_balance_temperature,
        state.input_transform.raw_white_balance_tint,
        context);
  } else {
    parameters.white_balance = multiply_matrix3(
        white_balance_matrix(
            effective_input_temperature(state),
            effective_input_tint(state),
            context),
        has_source_white_balance
            ? white_balance_matrix_between_whites(
                  source_temperature,
                  source_tint,
                  5500.0,
                  0.0,
                  context)
            : identity_matrix3());
  }
  return parameters;
}

[[nodiscard]] ColorPixel apply_input_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context) {
  auto pixel = apply_rgb_matrix(input, parameters.white_balance);
  const auto normalize_channel = [&](float channel) {
    const auto normalized = (static_cast<double>(channel) - parameters.raw_black_level) /
                            (parameters.raw_white_level - parameters.raw_black_level);
    return static_cast<float>(std::max(0.0, normalized));
  };
  pixel.red = normalize_channel(pixel.red);
  pixel.green = normalize_channel(pixel.green);
  pixel.blue = normalize_channel(pixel.blue);
  pixel = apply_rgb_matrix(pixel, parameters.camera_profile);
  pixel = apply_rgb_matrix(pixel, parameters.camera_matching_look);
  pixel = apply_rgb_matrix(pixel, parameters.calibration);
  pixel = apply_highlight_reconstruction(
      pixel,
      parameters.highlight_reconstruction_strength,
      context);
  pixel.red = static_cast<float>(std::max(0.0, static_cast<double>(pixel.red) * parameters.exposure_gain));
  pixel.green = static_cast<float>(std::max(0.0, static_cast<double>(pixel.green) * parameters.exposure_gain));
  pixel.blue = static_cast<float>(std::max(0.0, static_cast<double>(pixel.blue) * parameters.exposure_gain));
  return pixel;
}

void box_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* horizontal,
    std::vector<float>* output,
    CancellationToken cancellation = {}) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0 ||
      horizontal == nullptr || output == nullptr ||
      horizontal->size() != input.size() || output->size() != input.size() ||
      cancellation.cancelled()) {
    return;
  }

  const auto kernel_width = radius * 2 + 1;

  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_x = std::clamp(sample, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + clamped_x)];
    }

    for (int x = 0; x < width; ++x) {
      (*horizontal)[static_cast<std::size_t>(y * width + x)] =
          static_cast<float>(sum / kernel_width);
      const auto remove_x = std::clamp(x - radius, 0, width - 1);
      const auto add_x = std::clamp(x + radius + 1, 0, width - 1);
      sum += input[static_cast<std::size_t>(y * width + add_x)] -
             input[static_cast<std::size_t>(y * width + remove_x)];
    }
  });
  if (cancellation.cancelled()) {
    return;
  }

  parallel_for(width, [&](int x) {
    if (cancellation.cancelled()) {
      return;
    }
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_y = std::clamp(sample, 0, height - 1);
      sum += (*horizontal)[static_cast<std::size_t>(clamped_y * width + x)];
    }

    for (int y = 0; y < height; ++y) {
      (*output)[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum / kernel_width);
      const auto remove_y = std::clamp(y - radius, 0, height - 1);
      const auto add_y = std::clamp(y + radius + 1, 0, height - 1);
      sum += (*horizontal)[static_cast<std::size_t>(add_y * width + x)] -
             (*horizontal)[static_cast<std::size_t>(remove_y * width + x)];
    }
  });
}

[[nodiscard]] std::vector<float> box_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {}) {
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0) {
    return input;
  }
  if (cancellation.cancelled()) {
    return input;
  }

  std::vector<float> horizontal(input.size(), 0.0F);
  std::vector<float> output(input.size(), 0.0F);
  box_blur_into(input, width, height, radius, &horizontal, &output, cancellation);
  if (cancellation.cancelled()) {
    return input;
  }
  return output;
}

[[nodiscard]] std::array<int, 3> gaussian_approximation_radii(double sigma) {
  if (sigma <= 0.0) {
    return {0, 0, 0};
  }

  constexpr int kPassCount = 3;
  const auto ideal_width = std::sqrt((12.0 * sigma * sigma / static_cast<double>(kPassCount)) + 1.0);
  auto lower_width = static_cast<int>(std::floor(ideal_width));
  if ((lower_width % 2) == 0) {
    --lower_width;
  }
  lower_width = std::max(1, lower_width);
  const auto upper_width = lower_width + 2;
  const auto lower_count = std::clamp(static_cast<int>(std::round(
      (12.0 * sigma * sigma -
       static_cast<double>(kPassCount * lower_width * lower_width) -
       static_cast<double>(4 * kPassCount * lower_width) -
       static_cast<double>(3 * kPassCount)) /
      static_cast<double>(-4 * lower_width - 4))), 0, kPassCount);

  std::array<int, 3> radii{};
  for (int pass = 0; pass < kPassCount; ++pass) {
    const auto width = pass < lower_count ? lower_width : upper_width;
    radii[static_cast<std::size_t>(pass)] = std::max(0, (width - 1) / 2);
  }
  return radii;
}

struct BlurWorkspace final {
  std::vector<float> horizontal;
  std::vector<float> scratch;
};

void resize_blur_buffer(std::vector<float>* buffer, std::size_t size) {
  if (buffer != nullptr && buffer->size() != size) {
    buffer->resize(size);
  }
}

void gaussian_approximate_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation = {}) {
  if (output == nullptr) {
    return;
  }
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0) {
    *output = input;
    return;
  }
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  if (metal.available()) {
    auto metal_output = metal.gaussian_approximate_blur(input, width, height, radius);
    *output = cancellation.cancelled() ? input : std::move(metal_output);
    return;
  }
#endif

  const auto radii = gaussian_approximation_radii(static_cast<double>(radius));
  auto has_pass = false;
  for (const auto pass_radius : radii) {
    has_pass = has_pass || pass_radius > 0;
  }
  if (!has_pass) {
    *output = input;
    return;
  }

  BlurWorkspace local_workspace;
  auto& active_workspace = workspace != nullptr ? *workspace : local_workspace;
  resize_blur_buffer(&active_workspace.horizontal, input.size());
  resize_blur_buffer(&active_workspace.scratch, input.size());
  resize_blur_buffer(output, input.size());

  const auto* source = &input;
  auto* target = output;

  for (const auto pass_radius : radii) {
    if (pass_radius <= 0) {
      continue;
    }
    box_blur_into(
        *source,
        width,
        height,
        pass_radius,
        &active_workspace.horizontal,
        target,
        cancellation);
    if (cancellation.cancelled()) {
      *output = input;
      return;
    }
    source = target;
    target = target == output ? &active_workspace.scratch : output;
  }
  if (source != output) {
    output->swap(active_workspace.scratch);
  }
}

[[nodiscard]] std::vector<float> gaussian_approximate_blur(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {}) {
  std::vector<float> output;
  BlurWorkspace workspace;
  gaussian_approximate_blur_into(input, width, height, radius, &output, &workspace, cancellation);
  return output;
}

void guided_luminance_blur_into(
    const std::vector<float>& input,
    int width,
    int height,
    int radius,
    double epsilon,
    std::vector<float>* output,
    BlurWorkspace* workspace,
    CancellationToken cancellation = {}) {
  if (output == nullptr) {
    return;
  }
  if (radius <= 0 || input.empty() || width <= 0 || height <= 0) {
    *output = input;
    return;
  }
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  BlurWorkspace local_workspace;
  auto& active_workspace = workspace != nullptr ? *workspace : local_workspace;
  resize_blur_buffer(&active_workspace.horizontal, input.size());
  resize_blur_buffer(output, input.size());

  std::vector<float> mean(input.size(), 0.0F);
  std::vector<float> correlation(input.size(), 0.0F);
  std::vector<float> mean_correlation(input.size(), 0.0F);
  std::vector<float> coefficient_a(input.size(), 0.0F);
  std::vector<float> coefficient_b(input.size(), 0.0F);
  std::vector<float> mean_a(input.size(), 0.0F);
  std::vector<float> mean_b(input.size(), 0.0F);

  const auto blur_to = [&](const std::vector<float>& source, std::vector<float>* target) {
    box_blur_into(
        source,
        width,
        height,
        radius,
        &active_workspace.horizontal,
        target,
        cancellation);
  };

  blur_to(input, &mean);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto value = static_cast<double>(input[static_cast<std::size_t>(index)]);
    correlation[static_cast<std::size_t>(index)] = static_cast<float>(value * value);
  });
  blur_to(correlation, &mean_correlation);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  const auto safe_epsilon = std::max(1.0e-8, epsilon);
  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto sample_index = static_cast<std::size_t>(index);
    const auto local_mean = static_cast<double>(mean[sample_index]);
    const auto variance = std::max(
        0.0,
        static_cast<double>(mean_correlation[sample_index]) - local_mean * local_mean);
    const auto a = variance / (variance + safe_epsilon);
    coefficient_a[sample_index] = static_cast<float>(a);
    coefficient_b[sample_index] = static_cast<float>(local_mean * (1.0 - a));
  });

  blur_to(coefficient_a, &mean_a);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }
  blur_to(coefficient_b, &mean_b);
  if (cancellation.cancelled()) {
    *output = input;
    return;
  }

  parallel_for(static_cast<int>(input.size()), [&](int index) {
    const auto sample_index = static_cast<std::size_t>(index);
    const auto filtered =
        static_cast<double>(mean_a[sample_index]) *
            static_cast<double>(input[sample_index]) +
        static_cast<double>(mean_b[sample_index]);
    (*output)[sample_index] = static_cast<float>(std::max(0.0, filtered));
  });
}

[[nodiscard]] ColorPixel sample_bilinear(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y) {
  if (pixels.empty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto clamped_x = std::clamp(x, 0.0, static_cast<double>(width - 1));
  const auto clamped_y = std::clamp(y, 0.0, static_cast<double>(height - 1));
  const auto x0 = static_cast<int>(std::floor(clamped_x));
  const auto y0 = static_cast<int>(std::floor(clamped_y));
  const auto x1 = std::min(width - 1, x0 + 1);
  const auto y1 = std::min(height - 1, y0 + 1);
  const auto fx = clamped_x - static_cast<double>(x0);
  const auto fy = clamped_y - static_cast<double>(y0);

  const auto& top_left = pixels[static_cast<std::size_t>(y0 * width + x0)];
  const auto& top_right = pixels[static_cast<std::size_t>(y0 * width + x1)];
  const auto& bottom_left = pixels[static_cast<std::size_t>(y1 * width + x0)];
  const auto& bottom_right = pixels[static_cast<std::size_t>(y1 * width + x1)];

  const auto lerp = [](double a, double b, double t) { return a * (1.0 - t) + b * t; };
  const auto mix_channel = [&](auto ColorPixel::*member) {
    const auto top = lerp(top_left.*member, top_right.*member, fx);
    const auto bottom = lerp(bottom_left.*member, bottom_right.*member, fx);
    return lerp(top, bottom, fy);
  };

  return ColorPixel{
      .red = static_cast<float>(mix_channel(&ColorPixel::red)),
      .green = static_cast<float>(mix_channel(&ColorPixel::green)),
      .blue = static_cast<float>(mix_channel(&ColorPixel::blue)),
      .alpha = static_cast<float>(mix_channel(&ColorPixel::alpha)),
  };
}

[[nodiscard]] double mirrored_sample_coordinate(double value, int extent) {
  if (extent <= 1) {
    return 0.0;
  }
  const auto max_coordinate = static_cast<double>(extent - 1);
  const auto period = max_coordinate * 2.0;
  auto mirrored = std::fmod(value, period);
  if (mirrored < 0.0) {
    mirrored += period;
  }
  return mirrored <= max_coordinate ? mirrored : period - mirrored;
}

[[nodiscard]] ColorPixel sample_bilinear_crop_fill(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y,
    bool content_aware_fill) {
  if (!content_aware_fill) {
    return sample_bilinear(pixels, width, height, x, y);
  }
  return sample_bilinear(
      pixels,
      width,
      height,
      mirrored_sample_coordinate(x, width),
      mirrored_sample_coordinate(y, height));
}

void apply_optical_corrections(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (pixels == nullptr || pixels->empty() || width <= 1 || height <= 1) {
    return;
  }

  const auto distortion_strength = clamp_signed_percent(state.lens_distortion) / 100.0;
  const auto vignette_strength = clamp_signed_percent(state.lens_vignette) / 100.0;
  const auto ca_strength = clamp_unit(state.chromatic_aberration / 100.0);
  const auto vertical_perspective = clamp_signed_percent(state.perspective_vertical) / 100.0;
  const auto horizontal_perspective = clamp_signed_percent(state.perspective_horizontal) / 100.0;
  const auto transform_rotation = std::clamp(state.transform_rotation, -45.0, 45.0) * kPi / 180.0;
  const auto transform_scale = std::clamp(state.transform_scale, 0.25, 4.0);
  const auto transform_skew_x = clamp_signed_percent(state.transform_skew_x) / 100.0 * 0.60;
  const auto transform_skew_y = clamp_signed_percent(state.transform_skew_y) / 100.0 * 0.60;
  const auto liquify_strength = clamp_signed_percent(state.liquify_strength) / 100.0;
  const auto liquify_center_x = std::clamp(state.liquify_center_x, 0.0, 1.0) * static_cast<double>(width - 1);
  const auto liquify_center_y = std::clamp(state.liquify_center_y, 0.0, 1.0) * static_cast<double>(height - 1);
  const auto liquify_radius =
      std::clamp(state.liquify_radius, 0.02, 1.0) * std::max(static_cast<double>(width), static_cast<double>(height));
  if (std::abs(distortion_strength) <= 1e-6 &&
      std::abs(vignette_strength) <= 1e-6 &&
      ca_strength <= 1e-6 &&
      std::abs(vertical_perspective) <= 1e-6 &&
      std::abs(horizontal_perspective) <= 1e-6 &&
      std::abs(transform_rotation) <= 1e-6 &&
      std::abs(transform_scale - 1.0) <= 1e-6 &&
      std::abs(transform_skew_x) <= 1e-6 &&
      std::abs(transform_skew_y) <= 1e-6 &&
      std::abs(liquify_strength) <= 1e-6) {
    return;
  }

  const auto source = *pixels;
  std::vector<ColorPixel> corrected(source.size());
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  const auto max_radius = std::max(1.0, std::hypot(center_x, center_y));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto dx = static_cast<double>(x) - center_x;
      auto dy = static_cast<double>(y) - center_y;
      if (std::abs(transform_rotation) > 1e-6 ||
          std::abs(transform_scale - 1.0) > 1e-6 ||
          std::abs(transform_skew_x) > 1e-6 ||
          std::abs(transform_skew_y) > 1e-6) {
        dx /= transform_scale;
        dy /= transform_scale;
        const auto skewed_dx = dx + dy * transform_skew_x;
        const auto skewed_dy = dy + dx * transform_skew_y;
        const auto cosine = std::cos(-transform_rotation);
        const auto sine = std::sin(-transform_rotation);
        dx = skewed_dx * cosine - skewed_dy * sine;
        dy = skewed_dx * sine + skewed_dy * cosine;
      }
      const auto radius = std::hypot(dx, dy) / max_radius;
      const auto radius_sq = radius * radius;
      const auto distortion_scale =
          1.0 / std::max(0.40, 1.0 + distortion_strength * (0.55 * radius_sq + 0.20 * radius_sq * radius_sq));
      const auto normalized_dx = center_x > 0.0 ? dx / center_x : 0.0;
      const auto normalized_dy = center_y > 0.0 ? dy / center_y : 0.0;
      const auto vertical_scale =
          std::max(0.35, 1.0 + vertical_perspective * normalized_dy * 0.45);
      const auto horizontal_scale =
          std::max(0.35, 1.0 + horizontal_perspective * normalized_dx * 0.45);
      auto sample_x = center_x + dx * distortion_scale * vertical_scale;
      auto sample_y = center_y + dy * distortion_scale * horizontal_scale;
      if (std::abs(liquify_strength) > 1e-6) {
        const auto liquify_dx = sample_x - liquify_center_x;
        const auto liquify_dy = sample_y - liquify_center_y;
        const auto liquify_distance = std::hypot(liquify_dx, liquify_dy);
        if (liquify_distance < liquify_radius) {
          const auto falloff = 1.0 - smoothstep(0.0, 1.0, liquify_distance / std::max(1.0, liquify_radius));
          const auto liquify_scale = std::clamp(1.0 - liquify_strength * 0.55 * falloff, 0.30, 2.50);
          sample_x = liquify_center_x + liquify_dx * liquify_scale;
          sample_y = liquify_center_y + liquify_dy * liquify_scale;
        }
      }

      auto corrected_pixel =
          sample_bilinear_crop_fill(source, width, height, sample_x, sample_y, state.content_aware_crop_fill);
      if (ca_strength > 1e-6 && radius > 1e-6) {
        const auto inverse_length = 1.0 / std::max(1e-6, std::hypot(dx, dy));
        const auto radial_x = dx * inverse_length;
        const auto radial_y = dy * inverse_length;
        const auto shift = ca_strength * radius_sq * 2.2;
        const auto red_sample = sample_bilinear_crop_fill(
            source,
            width,
            height,
            sample_x - radial_x * shift * 1.15,
            sample_y - radial_y * shift * 1.15,
            state.content_aware_crop_fill);
        const auto blue_sample = sample_bilinear_crop_fill(
            source,
            width,
            height,
            sample_x - radial_x * shift * 0.85,
            sample_y - radial_y * shift * 0.85,
            state.content_aware_crop_fill);
        corrected_pixel.red = red_sample.red;
        corrected_pixel.blue = blue_sample.blue;
      }

      if (std::abs(vignette_strength) > 1e-6) {
        const auto edge_weight = smoothstep(0.22, 1.0, radius);
        const auto gain = std::max(0.20, 1.0 + vignette_strength * edge_weight * 0.85);
        corrected_pixel.red = static_cast<float>(static_cast<double>(corrected_pixel.red) * gain);
        corrected_pixel.green = static_cast<float>(static_cast<double>(corrected_pixel.green) * gain);
        corrected_pixel.blue = static_cast<float>(static_cast<double>(corrected_pixel.blue) * gain);
      }

      corrected[static_cast<std::size_t>(y * width + x)] = ColorPixel{
          .red = corrected_pixel.red,
          .green = corrected_pixel.green,
          .blue = corrected_pixel.blue,
          .alpha = static_cast<float>(clamp_unit(corrected_pixel.alpha)),
      };
    }
  }

  *pixels = std::move(corrected);
}

[[nodiscard]] OklabColor linear_rgb_to_oklab(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  const auto xyz = pixel_to_xyz_d65(pixel, context);

  const auto l = 0.8189330101 * xyz.x + 0.3618667424 * xyz.y - 0.1288597137 * xyz.z;
  const auto m = 0.0329845436 * xyz.x + 0.9293118715 * xyz.y + 0.0361456387 * xyz.z;
  const auto s = 0.0482003018 * xyz.x + 0.2643662691 * xyz.y + 0.6338517070 * xyz.z;

  const auto l_root = std::cbrt(std::max(0.0, l));
  const auto m_root = std::cbrt(std::max(0.0, m));
  const auto s_root = std::cbrt(std::max(0.0, s));

  return OklabColor{
      .lightness = 0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
      .a = 1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
      .b = 0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root,
  };
}

[[nodiscard]] ColorPixel oklab_to_linear_rgb(
    const OklabColor& color,
    double alpha,
    const PipelineColorContext& context) {
  const auto l_root = color.lightness + 0.3963377774 * color.a + 0.2158037573 * color.b;
  const auto m_root = color.lightness - 0.1055613458 * color.a - 0.0638541728 * color.b;
  const auto s_root = color.lightness - 0.0894841775 * color.a - 1.2914855480 * color.b;

  const auto l = l_root * l_root * l_root;
  const auto m = m_root * m_root * m_root;
  const auto s = s_root * s_root * s_root;

  return apply_rgb_matrix(
      ColorPixel{
          .red = static_cast<float>(+1.2270138511 * l - 0.5577999807 * m + 0.2812561490 * s),
          .green = static_cast<float>(-0.0405801784 * l + 1.1122568696 * m - 0.0716766787 * s),
          .blue = static_cast<float>(-0.0763812845 * l - 0.4214819784 * m + 1.5861632204 * s),
          .alpha = static_cast<float>(alpha),
      },
      context.xyz_d65_to_rgb);
}

[[nodiscard]] double oklab_lightness_ceiling(const PipelineColorContext& context) {
  return std::cbrt(std::clamp(context.output_headroom, 1.0, 16.0));
}

[[nodiscard]] double clamp_oklab_lightness(
    double lightness,
    const PipelineColorContext& context) {
  return std::clamp(lightness, 0.0, oklab_lightness_ceiling(context));
}

[[nodiscard]] bool in_output_gamut(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  const auto headroom = std::clamp(context.output_headroom, 1.0, 16.0);
  return pixel.red >= -1e-6 && pixel.red <= headroom + 1e-6 &&
         pixel.green >= -1e-6 && pixel.green <= headroom + 1e-6 &&
         pixel.blue >= -1e-6 && pixel.blue <= headroom + 1e-6;
}

[[nodiscard]] ColorPixel clamp_rgb_to_output_headroom(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  auto clamped = fit_pixel_to_output_headroom(pixel, context.output_headroom);
  clamped.alpha = static_cast<float>(clamp_unit(clamped.alpha));
  return clamped;
}

[[nodiscard]] Matrix3 white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel gamut_fit_oklch(
    double lightness,
    double chroma,
    double hue,
    double alpha,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel gamut_fit_oklab(
    double lightness,
    double a,
    double b,
    double alpha,
    const PipelineColorContext& context);

void apply_local_adjustment_mask(
    ColorPixel* pixel,
    const PipelineColorContext& context,
    double weight,
    double exposure,
    double contrast,
    double highlights,
    double shadows,
    double whites,
    double blacks,
    double temperature,
    double tint,
    double vibrance,
    double saturation,
    double hue_shift,
    double saturation_shift,
    double colorize_hue,
    double colorize_strength) {
  if (pixel == nullptr || weight <= 0.0) {
    return;
  }

  const auto weighted_contrast = contrast * weight;
  const auto weighted_highlights = highlights * weight;
  const auto weighted_shadows = shadows * weight;
  const auto weighted_whites = whites * weight;
  const auto weighted_blacks = blacks * weight;
  const auto has_local_tone =
      std::abs(weighted_contrast) > 1e-6 ||
      std::abs(weighted_highlights) > 1e-6 ||
      std::abs(weighted_shadows) > 1e-6 ||
      std::abs(weighted_whites) > 1e-6 ||
      std::abs(weighted_blacks) > 1e-6;
  const auto weighted_vibrance = vibrance * weight;
  const auto weighted_saturation = saturation * weight;
  const auto has_local_chroma =
      std::abs(weighted_vibrance) > 1e-6 ||
      std::abs(weighted_saturation) > 1e-6 ||
      std::abs(hue_shift) > 1e-6 ||
      std::abs(saturation_shift) > 1e-6 ||
      std::abs(colorize_strength) > 1e-6;
  const auto has_local_white_balance =
      std::abs(temperature) > 1e-6 || std::abs(tint) > 1e-6;

  if (!has_local_white_balance && !has_local_tone && !has_local_chroma) {
    if (std::abs(exposure) > 1e-6) {
      const auto local_exposure_gain = std::pow(2.0, exposure * weight);
      pixel->red = static_cast<float>(static_cast<double>(pixel->red) * local_exposure_gain);
      pixel->green = static_cast<float>(static_cast<double>(pixel->green) * local_exposure_gain);
      pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * local_exposure_gain);
    }
    return;
  }

  const auto local_white_balance = white_balance_matrix(temperature * weight, tint * weight, context);
  const auto local_exposure_gain = std::pow(2.0, exposure * weight);
  const auto balanced = apply_rgb_matrix(*pixel, local_white_balance);
  pixel->red = static_cast<float>(std::lerp(static_cast<double>(pixel->red), static_cast<double>(balanced.red), clamp_unit(weight)));
  pixel->green = static_cast<float>(std::lerp(static_cast<double>(pixel->green), static_cast<double>(balanced.green), clamp_unit(weight)));
  pixel->blue = static_cast<float>(std::lerp(static_cast<double>(pixel->blue), static_cast<double>(balanced.blue), clamp_unit(weight)));
  pixel->red = static_cast<float>(static_cast<double>(pixel->red) * local_exposure_gain);
  pixel->green = static_cast<float>(static_cast<double>(pixel->green) * local_exposure_gain);
  pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * local_exposure_gain);

  if (has_local_tone) {
    ColorAdjustmentState local_tone_state;
    local_tone_state.contrast = weighted_contrast;
    local_tone_state.highlights = weighted_highlights;
    local_tone_state.shadows = weighted_shadows;
    local_tone_state.whites = weighted_whites;
    local_tone_state.blacks = weighted_blacks;
    *pixel = scale_pixel_to_luminance(
        *pixel,
        apply_global_tone(luminance_for_pixel(*pixel, context), local_tone_state),
        context);
  }

  if (!has_local_chroma) {
    return;
  }

  auto oklab = linear_rgb_to_oklab(*pixel, context);
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  if (chroma <= 1e-6) {
    hue = wrap_unit(colorize_hue) * 2.0 * kPi;
  } else {
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    const auto mid_lightness_weight = 1.0 - smoothstep(0.18, 0.48, std::abs(lightness - 0.56));
    const auto tonal_protection = tone_chroma_protection(lightness);
    const auto skin_protection =
        default_skin_chroma_protection(normalized_hue, chroma);
    chroma *= std::max(
        0.0,
        1.0 + (clamp_signed_percent(weighted_vibrance) / 100.0) * low_chroma_weight *
                  mid_lightness_weight * tonal_protection * skin_protection * 0.82);
    const auto weighted_saturation_control = clamp_signed_percent(weighted_saturation) / 100.0;
    if (weighted_saturation_control >= 0.0) {
      const auto saturation_tonal_weight = 0.45 + tonal_protection * 0.55;
      chroma *= std::max(
          0.0,
          1.0 + weighted_saturation_control * 0.72 * saturation_tonal_weight * skin_protection);
    } else {
      chroma *= std::max(0.0, 1.0 + weighted_saturation_control);
    }
    chroma *= highlight_chroma_compression_scale(lightness, chroma);
  }

  hue = wrap_radians(hue + (clamp_signed_percent(hue_shift) / 100.0) * kPi * weight);
  chroma *= std::max(
      0.0,
      1.0 + (clamp_signed_percent(saturation_shift) / 100.0) * 0.92 * weight);

  const auto colorize_amount = clamp_unit(colorize_strength / 100.0) * weight;
  if (colorize_amount > 1e-6) {
    const auto target_hue = wrap_unit(colorize_hue) * 2.0 * kPi;
    hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * colorize_amount);
    chroma = std::lerp(chroma, std::max(chroma, 0.18), colorize_amount);
  }

  *pixel = gamut_fit_oklch(lightness, chroma, hue, pixel->alpha, context);
}

void apply_mask_edge_cleanup(
    ColorPixel* pixel,
    const PipelineColorContext& context,
    double mask_weight,
    double decontaminate) {
  if (pixel == nullptr) {
    return;
  }

  const auto cleanup_strength =
      clamp_unit(decontaminate / 100.0) *
      smoothstep(0.02, 0.24, clamp_unit(mask_weight) * (1.0 - clamp_unit(mask_weight)) * 4.0);
  if (cleanup_strength <= 1e-6) {
    return;
  }

  auto oklab = linear_rgb_to_oklab(*pixel, context);
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  const auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  chroma *= std::max(0.0, 1.0 - cleanup_strength * 0.75);
  lightness = clamp_unit(lightness + cleanup_strength * 0.015);
  *pixel = gamut_fit_oklch(lightness, chroma, hue, pixel->alpha, context);
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorAdjustmentState& state) {
  if (!state.radial_mask_enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, state.radial_mask_radius_x);
  const auto radius_y = std::max(0.05, state.radial_mask_radius_y);
  const auto feather = std::clamp(state.radial_mask_feather, 0.05, 1.0);
  const auto radians = state.radial_mask_rotation * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - state.radial_mask_center_x;
  const auto translated_y = normalized_y - state.radial_mask_center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double linear_mask_weight(
    double normalized_x,
    double normalized_y,
    double position,
    double feather,
    double rotation_degrees) {
  const auto radians = rotation_degrees * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto axis_coordinate = ((normalized_x - 0.5) * cosine + (normalized_y - 0.5) * sine) + 0.5;
  return clamp_unit(((position - axis_coordinate) / std::max(0.05, feather)) + 0.5);
}

[[nodiscard]] double apply_selection_expand(double base_weight, const LocalMask& mask) {
  const auto expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  if (std::abs(expand) <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto threshold = std::clamp(0.5 - expand * 0.35, 0.08, 0.92);
  const auto ramp = std::clamp(0.06 + std::abs(expand) * 0.18, 0.04, 0.28);
  return smoothstep(threshold - ramp, threshold + ramp, clamp_unit(base_weight));
}

[[nodiscard]] double apply_selection_edge_contrast(double base_weight, const LocalMask& mask) {
  const auto contrast = clamp_unit(mask.selection_edge_contrast / 100.0);
  if (contrast <= 1e-6) {
    return clamp_unit(base_weight);
  }

  const auto ramp = std::lerp(0.24, 0.035, contrast);
  return smoothstep(0.5 - ramp, 0.5 + ramp, clamp_unit(base_weight));
}

[[nodiscard]] double apply_selection_smooth(double base_weight, const LocalMask& mask) {
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (smoothing <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  return std::lerp(weight, 0.5, smoothing * 0.35);
}

[[nodiscard]] double apply_selection_fine_edge(double base_weight, const LocalMask& mask) {
  const auto refinement = clamp_unit(mask.selection_fine_edge / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (refinement <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6) {
    return weight;
  }

  const auto edge_band = 1.0 - std::abs((weight * 2.0) - 1.0);
  const auto strand_lift = std::sqrt(weight);
  const auto lifted = std::lerp(weight, strand_lift, refinement * 0.55 * edge_band);
  const auto edge_softened = std::lerp(lifted, 0.5, refinement * 0.12 * edge_band);
  return clamp_unit(edge_softened);
}

[[nodiscard]] double apply_selection_refine(
    double base_weight,
    const LocalMask& mask,
    bool spatial_expand_smooth_applied = false) {
  if (spatial_expand_smooth_applied) {
    auto refined = clamp_unit(base_weight);
    refined = apply_selection_fine_edge(refined, mask);
    refined = apply_selection_edge_contrast(refined, mask);
    if (mask.invert || mask.selection_invert) {
      refined = 1.0 - refined;
    }
    refined = std::min(refined, std::clamp(mask.density, 0.0, 1.0));
    return clamp_unit(refined * std::clamp(mask.opacity, 0.0, 1.0));
  } else {
    auto refined = apply_selection_expand(base_weight, mask);
    refined = apply_selection_smooth(refined, mask);
    refined = apply_selection_fine_edge(refined, mask);
    refined = apply_selection_edge_contrast(refined, mask);
    if (mask.invert || mask.selection_invert) {
      refined = 1.0 - refined;
    }
    refined = std::min(refined, std::clamp(mask.density, 0.0, 1.0));
    return clamp_unit(refined * std::clamp(mask.opacity, 0.0, 1.0));
  }
}

[[nodiscard]] double radial_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.05, mask.radius_x);
  const auto radius_y = std::max(0.05, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.05, 1.0);
  const auto radians = mask.rotation * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::sqrt((local_x * local_x) / (radius_x * radius_x) +
                                  (local_y * local_y) / (radius_y * radius_y));
  const auto inner_radius = std::max(0.0, 1.0 - feather);
  if (distance <= inner_radius) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_radius, 1.0, distance);
}

[[nodiscard]] double rectangle_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  if (!mask.enabled) {
    return 0.0;
  }

  const auto radius_x = std::max(0.01, mask.radius_x);
  const auto radius_y = std::max(0.01, mask.radius_y);
  const auto feather = std::clamp(mask.feather, 0.0, 1.0);
  const auto radians = mask.rotation * kPi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto translated_x = normalized_x - mask.center_x;
  const auto translated_y = normalized_y - mask.center_y;
  const auto local_x = translated_x * cosine + translated_y * sine;
  const auto local_y = -translated_x * sine + translated_y * cosine;
  const auto distance = std::max(std::abs(local_x) / radius_x, std::abs(local_y) / radius_y);
  if (feather <= 1e-6) {
    return distance <= 1.0 ? 1.0 : 0.0;
  }
  const auto inner_edge = std::max(0.0, 1.0 - feather);
  if (distance <= inner_edge) {
    return 1.0;
  }
  if (distance >= 1.0) {
    return 0.0;
  }
  return 1.0 - smoothstep(inner_edge, 1.0, distance);
}

[[nodiscard]] double brush_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (!mask.segmentation_mask.empty() && mask.segmentation_width > 0 &&
      mask.segmentation_height > 0 &&
      mask.segmentation_mask.size() == expected_mask_size) {
    const auto max_x = std::max(mask.segmentation_width - 1, 0);
    const auto max_y = std::max(mask.segmentation_height - 1, 0);
    const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
    const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
    const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
    const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
    const auto ix1 = std::min(ix0 + 1, max_x);
    const auto iy1 = std::min(iy0 + 1, max_y);
    const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
    const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
    const auto index_for = [&](int x, int y) {
      return static_cast<std::size_t>(y * mask.segmentation_width + x);
    };
    const auto sample = [&](int x, int y) {
      return static_cast<double>(mask.segmentation_mask[index_for(x, y)]) / 255.0;
    };
    const auto v00 = sample(ix0, iy0);
    const auto v10 = sample(ix1, iy0);
    const auto v01 = sample(ix0, iy1);
    const auto v11 = sample(ix1, iy1);
    const auto top = v00 * (1.0 - dx) + v10 * dx;
    const auto bottom = v01 * (1.0 - dx) + v11 * dx;
    return clamp_unit(top * (1.0 - dy) + bottom * dy);
  }

  return 0.0;
}

[[nodiscard]] double distance_to_segment(
    double px,
    double py,
    double ax,
    double ay,
    double bx,
    double by) {
  const auto abx = bx - ax;
  const auto aby = by - ay;
  const auto ab_len_sq = abx * abx + aby * aby;
  if (ab_len_sq <= 1e-9) {
    return std::hypot(px - ax, py - ay);
  }

  const auto t = std::clamp(((px - ax) * abx + (py - ay) * aby) / ab_len_sq, 0.0, 1.0);
  const auto closest_x = ax + abx * t;
  const auto closest_y = ay + aby * t;
  return std::hypot(px - closest_x, py - closest_y);
}

[[nodiscard]] bool polygon_contains_point(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return false;
  }

  auto inside = false;
  for (std::size_t i = 0, j = point_count - 1; i < point_count; j = i++) {
    const auto& pi = mask.polygon_points[i];
    const auto& pj = mask.polygon_points[j];
    const auto intersects =
        ((pi.y > normalized_y) != (pj.y > normalized_y)) &&
        (normalized_x <
         ((pj.x - pi.x) * (normalized_y - pi.y) / std::max(1e-9, pj.y - pi.y)) + pi.x);
    if (intersects) {
      inside = !inside;
    }
  }
  return inside;
}

[[nodiscard]] double polygon_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto point_count = mask.polygon_points.size();
  if (point_count < 3) {
    return 0.0;
  }

  auto min_distance = std::numeric_limits<double>::max();
  for (std::size_t i = 0; i < point_count; ++i) {
    const auto& start = mask.polygon_points[i];
    const auto& end = mask.polygon_points[(i + 1) % point_count];
    min_distance = std::min(
        min_distance,
        distance_to_segment(normalized_x, normalized_y, start.x, start.y, end.x, end.y));
  }

  const auto feather = std::clamp(mask.feather, 0.0, 0.25);
  if (polygon_contains_point(normalized_x, normalized_y, mask)) {
    return 1.0;
  }
  if (feather <= 1e-6) {
    return 0.0;
  }
  return 1.0 - smoothstep(0.0, feather, min_distance);
}

void hue_saturation_value_for_pixel(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    double* hue,
    double* saturation,
    double* value) {
  const auto reference_pixel = pixel_to_linear_srgb_reference(pixel, context);
  const auto max_channel = std::max({reference_pixel.red, reference_pixel.green, reference_pixel.blue});
  const auto min_channel = std::min({reference_pixel.red, reference_pixel.green, reference_pixel.blue});
  const auto delta = max_channel - min_channel;

  if (value != nullptr) {
    *value = clamp_unit(max_channel);
  }
  if (saturation != nullptr) {
    *saturation = max_channel <= 1e-6 ? 0.0 : clamp_unit(delta / max_channel);
  }
  if (hue == nullptr) {
    return;
  }

  if (delta <= 1e-6) {
    *hue = 0.0;
    return;
  }

  auto result = 0.0;
  if (max_channel == reference_pixel.red) {
    result = std::fmod((reference_pixel.green - reference_pixel.blue) / delta, 6.0);
  } else if (max_channel == reference_pixel.green) {
    result = ((reference_pixel.blue - reference_pixel.red) / delta) + 2.0;
  } else {
    result = ((reference_pixel.red - reference_pixel.green) / delta) + 4.0;
  }

  result /= 6.0;
  if (result < 0.0) {
    result += 1.0;
  }
  *hue = wrap_unit(result);
}

[[nodiscard]] double luminance_range_mask_weight(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto min_value = std::clamp(mask.luminance_min, 0.0, 1.0);
  auto max_value = std::clamp(mask.luminance_max, 0.0, 1.0);
  if (min_value > max_value) {
    std::swap(min_value, max_value);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto lower_weight = min_value <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, min_value - feather), min_value, value);
  const auto upper_weight = max_value >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      max_value,
                                      std::min(1.0, max_value + feather),
                                      value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double color_range_mask_weight(
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  double hue = 0.0;
  double saturation = 0.0;
  hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
  auto saturation_min = std::clamp(mask.saturation_min, 0.0, 1.0);
  auto saturation_max = std::clamp(mask.saturation_max, 0.0, 1.0);
  if (saturation_min > saturation_max) {
    std::swap(saturation_min, saturation_max);
  }
  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto hue_weight = circular_weight(hue, wrap_unit(mask.hue_center), std::max(0.02, mask.hue_range));
  const auto sat_lower =
      smoothstep(std::max(0.0, saturation_min - feather), saturation_min, saturation);
  const auto sat_upper =
      1.0 - smoothstep(saturation_max, std::min(1.0, saturation_max + feather), saturation);
  return clamp_unit(hue_weight * sat_lower * sat_upper);
}

[[nodiscard]] double depth_range_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto min_value = std::clamp(mask.depth_min, 0.0, 1.0);
  auto max_value = std::clamp(mask.depth_max, 0.0, 1.0);
  if (min_value > max_value) {
    std::swap(min_value, max_value);
  }

  auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto expected_map_size =
      static_cast<std::size_t>(mask.depth_width) * static_cast<std::size_t>(mask.depth_height);
  if (!mask.depth_map.empty() && mask.depth_width > 0 && mask.depth_height > 0 &&
      mask.depth_map.size() == expected_map_size) {
    const auto max_x = std::max(mask.depth_width - 1, 0);
    const auto max_y = std::max(mask.depth_height - 1, 0);
    const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
    const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
    const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
    const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
    const auto ix1 = std::min(ix0 + 1, max_x);
    const auto iy1 = std::min(iy0 + 1, max_y);
    const auto dx = mask.depth_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
    const auto dy = mask.depth_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
    const auto idx = [&](int x, int y) {
      return static_cast<std::size_t>(y * mask.depth_width + x);
    };
    const auto sample = [&](int x, int y) {
      return static_cast<double>(mask.depth_map[idx(x, y)]) / 255.0;
    };
    const auto top = sample(ix0, iy0) * (1.0 - dx) + sample(ix1, iy0) * dx;
    const auto bottom = sample(ix0, iy1) * (1.0 - dx) + sample(ix1, iy1) * dx;
    value = clamp_unit(top * (1.0 - dy) + bottom * dy);
  }

  const auto feather = std::clamp(mask.range_feather, 0.0, 0.5);
  const auto lower_weight = min_value <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, min_value - feather), min_value, value);
  const auto upper_weight = max_value >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      max_value,
                                      std::min(1.0, max_value + feather),
                                      value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double unit_range_weight(double value, double min_value, double max_value, double feather) {
  auto minimum = std::clamp(min_value, 0.0, 1.0);
  auto maximum = std::clamp(max_value, 0.0, 1.0);
  if (minimum > maximum) {
    std::swap(minimum, maximum);
  }
  const auto clamped_value = clamp_unit(value);
  const auto clamped_feather = std::clamp(feather, 0.0, 0.5);
  const auto lower_weight = minimum <= 1.0e-9
                                ? 1.0
                                : smoothstep(std::max(0.0, minimum - clamped_feather), minimum, clamped_value);
  const auto upper_weight = maximum >= 1.0 - 1.0e-9
                                ? 1.0
                                : 1.0 - smoothstep(
                                      maximum,
                                      std::min(1.0, maximum + clamped_feather),
                                      clamped_value);
  return clamp_unit(lower_weight * upper_weight);
}

[[nodiscard]] double sampled_depth_value(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto value = clamp_unit(luminance_for_pixel(pixel, context));
  const auto expected_map_size =
      static_cast<std::size_t>(mask.depth_width) * static_cast<std::size_t>(mask.depth_height);
  if (mask.depth_map.empty() || mask.depth_width <= 0 || mask.depth_height <= 0 ||
      mask.depth_map.size() != expected_map_size) {
    return value;
  }

  const auto max_x = std::max(mask.depth_width - 1, 0);
  const auto max_y = std::max(mask.depth_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.depth_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.depth_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.depth_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.depth_map[idx(x, y)]) / 255.0;
  };
  const auto top = sample(ix0, iy0) * (1.0 - dx) + sample(ix1, iy0) * dx;
  const auto bottom = sample(ix0, iy1) * (1.0 - dx) + sample(ix1, iy1) * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

[[nodiscard]] double apply_range_refinement(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  auto weight = clamp_unit(base_weight);
  if (weight <= 0.0) {
    return 0.0;
  }
  const auto feather = std::clamp(mask.refine_range_feather, 0.0, 0.5);
  if (mask.refine_luminance_min > 1.0e-9 || mask.refine_luminance_max < 1.0 - 1.0e-9) {
    weight *= unit_range_weight(
        luminance_for_pixel(pixel, context),
        mask.refine_luminance_min,
        mask.refine_luminance_max,
        feather);
  }
  if (mask.refine_hue_range < 0.5 - 1.0e-9) {
    double hue = 0.0;
    double saturation = 0.0;
    hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
    weight *= circular_weight(hue, wrap_unit(mask.refine_hue_center), std::max(0.02, mask.refine_hue_range));
  }
  if (mask.refine_saturation_min > 1.0e-9 || mask.refine_saturation_max < 1.0 - 1.0e-9) {
    double hue = 0.0;
    double saturation = 0.0;
    hue_saturation_value_for_pixel(pixel, context, &hue, &saturation, nullptr);
    weight *= unit_range_weight(
        saturation,
        mask.refine_saturation_min,
        mask.refine_saturation_max,
        feather);
  }
  if (mask.refine_depth_min > 1.0e-9 || mask.refine_depth_max < 1.0 - 1.0e-9) {
    weight *= unit_range_weight(
        sampled_depth_value(normalized_x, normalized_y, pixel, context, mask),
        mask.refine_depth_min,
        mask.refine_depth_max,
        feather);
  }
  if (mask.refine_edge_confidence_min > 1.0e-9) {
    const auto edge_confidence = clamp_unit(std::abs(weight - 0.5) * 2.0);
    weight *= smoothstep(
        std::clamp(mask.refine_edge_confidence_min, 0.0, 1.0),
        1.0,
        edge_confidence);
  }
  return clamp_unit(weight);
}

[[nodiscard]] double subject_mask_weight(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  // Map normalized coordinates to mask bitmap with bilinear sampling.
  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;

  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };

  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[idx(x, y)]) / 255.0;
  };

  // Bilinear interpolation for smooth edges
  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  const auto value = top * (1.0 - dy) + bottom * dy;

  // Apply feather as a threshold ramp for edge softness control
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

[[nodiscard]] bool is_ai_segmentation_mask_kind(const std::string& kind) {
  return kind == "alpha" || kind == "subject" || kind == "sky" || kind == "background" ||
         kind == "people" || kind == "face" || kind == "hair" ||
         kind == "clothing" || kind == "object";
}

[[nodiscard]] bool mask_supports_edge_aware_matte(const LocalMask& mask) {
  return is_ai_segmentation_mask_kind(mask.kind) || mask.kind == "brush";
}

[[nodiscard]] bool raster_morphology_available(const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  return mask_supports_edge_aware_matte(mask) &&
         !mask.segmentation_mask.empty() &&
         mask.segmentation_width > 0 &&
         mask.segmentation_height > 0 &&
         mask.segmentation_mask.size() == expected_mask_size;
}

[[nodiscard]] double raw_segmentation_mask_value(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask) {
  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return 0.0;
  }

  const auto max_x = std::max(mask.segmentation_width - 1, 0);
  const auto max_y = std::max(mask.segmentation_height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  const auto ix0 = std::clamp(static_cast<int>(fx), 0, max_x);
  const auto iy0 = std::clamp(static_cast<int>(fy), 0, max_y);
  const auto ix1 = std::min(ix0 + 1, max_x);
  const auto iy1 = std::min(iy0 + 1, max_y);
  const auto dx = mask.segmentation_width > 1 ? fx - static_cast<double>(ix0) : 0.0;
  const auto dy = mask.segmentation_height > 1 ? fy - static_cast<double>(iy0) : 0.0;
  const auto idx = [&](int x, int y) {
    return static_cast<std::size_t>(y * mask.segmentation_width + x);
  };
  const auto sample = [&](int x, int y) {
    return static_cast<double>(mask.segmentation_mask[idx(x, y)]) / 255.0;
  };

  const auto v00 = sample(ix0, iy0);
  const auto v10 = sample(ix1, iy0);
  const auto v01 = sample(ix0, iy1);
  const auto v11 = sample(ix1, iy1);
  const auto top = v00 * (1.0 - dx) + v10 * dx;
  const auto bottom = v01 * (1.0 - dx) + v11 * dx;
  return clamp_unit(top * (1.0 - dy) + bottom * dy);
}

[[nodiscard]] double raster_selection_weight_from_raw_value(double raw_value, const LocalMask& mask) {
  const auto value = clamp_unit(raw_value);
  if (!is_ai_segmentation_mask_kind(mask.kind)) {
    return value;
  }
  const auto feather = std::max(0.01, mask.feather);
  const auto low = std::max(0.0, 0.5 - feather * 0.5);
  const auto high = std::min(1.0, 0.5 + feather * 0.5);
  return smoothstep(low, high, value);
}

[[nodiscard]] double raster_morphology_sample_spacing_x(const LocalMask& mask) {
  return mask.segmentation_width > 1 ? 1.0 / static_cast<double>(mask.segmentation_width - 1) : 1.0;
}

[[nodiscard]] double raster_morphology_sample_spacing_y(const LocalMask& mask) {
  return mask.segmentation_height > 1 ? 1.0 / static_cast<double>(mask.segmentation_height - 1) : 1.0;
}

[[nodiscard]] int raster_morphology_radius(double amount, const LocalMask& mask) {
  const auto raster_extent = std::max(mask.segmentation_width, mask.segmentation_height);
  return std::clamp(
      static_cast<int>(std::ceil(std::abs(amount) * static_cast<double>(std::max(1, raster_extent)))),
      1,
      24);
}

[[nodiscard]] ColorPixel sample_edge_matte_source(
    double normalized_x,
    double normalized_y,
    const EdgeAwareMatteContext& source_context) {
  if (source_context.pixels == nullptr || source_context.width <= 0 || source_context.height <= 0) {
    return {};
  }
  const auto max_x = std::max(source_context.width - 1, 0);
  const auto max_y = std::max(source_context.height - 1, 0);
  const auto fx = clamp_unit(normalized_x) * static_cast<double>(max_x);
  const auto fy = clamp_unit(normalized_y) * static_cast<double>(max_y);
  return sample_bilinear(*source_context.pixels, source_context.width, source_context.height, fx, fy);
}

[[nodiscard]] double edge_matte_color_distance(const ColorPixel& left, const ColorPixel& right) {
  const auto dr = static_cast<double>(left.red) - static_cast<double>(right.red);
  const auto dg = static_cast<double>(left.green) - static_cast<double>(right.green);
  const auto db = static_cast<double>(left.blue) - static_cast<double>(right.blue);
  const auto dl = (dr * 0.2126) + (dg * 0.7152) + (db * 0.0722);
  return std::sqrt((dr * dr * 0.42) + (dg * dg * 0.42) + (db * db * 0.42) + (dl * dl * 0.58));
}

[[nodiscard]] double local_raw_matte_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_weight = 1.0 / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_edge_aware_raw_matte_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius,
    const EdgeAwareMatteContext* source_context) {
  if (source_context == nullptr || source_context->pixels == nullptr ||
      source_context->width <= 0 || source_context->height <= 0) {
    return local_raw_matte_average(normalized_x, normalized_y, mask, radius);
  }

  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  const auto center_source = sample_edge_matte_source(normalized_x, normalized_y, *source_context);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_x = normalized_x + static_cast<double>(offset_x) * step_x;
      const auto sample_y = normalized_y + static_cast<double>(offset_y) * step_y;
      const auto sample_source = sample_edge_matte_source(sample_x, sample_y, *source_context);
      const auto source_distance = edge_matte_color_distance(center_source, sample_source);
      const auto source_gate = std::max(0.03, 1.0 - smoothstep(0.035, 0.22, source_distance));
      const auto sample_weight = source_gate / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(sample_x, sample_y, mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_raw_matte_neighbor_average(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  auto sum = 0.0;
  auto total_weight = 0.0;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      const auto sample_weight = 1.0 / (1.0 + distance);
      const auto sample = raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask);
      sum += sample * sample_weight;
      total_weight += sample_weight;
    }
  }
  return total_weight > 1e-6 ? clamp_unit(sum / total_weight)
                             : raw_segmentation_mask_value(normalized_x, normalized_y, mask);
}

[[nodiscard]] double local_raw_matte_median(
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    int radius) {
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(radius) + 1e-6) {
        continue;
      }
      samples.push_back(raw_segmentation_mask_value(
          normalized_x + static_cast<double>(offset_x) * step_x,
          normalized_y + static_cast<double>(offset_y) * step_y,
          mask));
    }
  }
  if (samples.empty()) {
    return raw_segmentation_mask_value(normalized_x, normalized_y, mask);
  }
  const auto middle = samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
  std::nth_element(samples.begin(), middle, samples.end());
  return clamp_unit(*middle);
}

[[nodiscard]] double apply_raster_selection_morphology(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context,
    bool* applied) {
  if (applied != nullptr) {
    *applied = false;
  }
  if (!raster_morphology_available(mask)) {
    return clamp_unit(base_weight);
  }

  const auto legacy_expand = clamp_signed_percent(mask.selection_expand) / 100.0;
  const auto grow = clamp_unit(mask.selection_grow / 100.0) + std::max(0.0, legacy_expand);
  const auto shrink = clamp_unit(mask.selection_shrink / 100.0) + std::max(0.0, -legacy_expand);
  const auto feather = clamp_unit(mask.selection_feather / 100.0);
  const auto blur = clamp_unit(mask.selection_blur / 100.0);
  const auto harden = clamp_unit(mask.selection_harden / 100.0);
  const auto despeckle = clamp_unit(mask.selection_despeckle / 100.0);
  const auto fill_holes = clamp_unit(mask.selection_fill_holes / 100.0);
  const auto remove_islands = clamp_unit(mask.selection_remove_islands / 100.0);
  const auto smoothing = clamp_unit(mask.selection_smooth / 100.0);
  if (grow <= 1e-6 && shrink <= 1e-6 && feather <= 1e-6 && blur <= 1e-6 &&
      harden <= 1e-6 && despeckle <= 1e-6 && fill_holes <= 1e-6 &&
      remove_islands <= 1e-6 && smoothing <= 1e-6) {
    return clamp_unit(base_weight);
  }

  auto raw_value = raw_segmentation_mask_value(normalized_x, normalized_y, mask);
  const auto step_x = raster_morphology_sample_spacing_x(mask);
  const auto step_y = raster_morphology_sample_spacing_y(mask);
  if (fill_holes > 1e-6 || remove_islands > 1e-6) {
    const auto neighbor_average = local_raw_matte_neighbor_average(normalized_x, normalized_y, mask, 1);
    if (fill_holes > 1e-6 && raw_value < 0.5 && neighbor_average >= 0.50) {
      raw_value = std::lerp(raw_value, neighbor_average, fill_holes);
    }
    if (remove_islands > 1e-6 && raw_value > 0.5 && neighbor_average <= 0.50) {
      raw_value = std::lerp(raw_value, neighbor_average, remove_islands);
    }
  }

  if (despeckle > 1e-6) {
    raw_value = std::lerp(
        raw_value,
        local_raw_matte_median(normalized_x, normalized_y, mask, raster_morphology_radius(despeckle, mask) / 2 + 1),
        despeckle);
  }

  if (grow > 1e-6) {
    const auto radius = raster_morphology_radius(grow, mask);
    auto extreme = 0.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        extreme = std::max(extreme, sample);
      }
    }
    raw_value = std::lerp(raw_value, extreme, std::min(1.0, grow));
  }

  if (shrink > 1e-6) {
    const auto radius = raster_morphology_radius(shrink, mask);
    auto extreme = 1.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        extreme = std::min(extreme, sample);
      }
    }
    raw_value = std::lerp(raw_value, extreme, std::min(1.0, shrink));
  }

  if (feather > 1e-6) {
    const auto radius = std::min(raster_morphology_radius(feather, mask), 12);
    raw_value = std::lerp(
        raw_value,
        local_edge_aware_raw_matte_average(normalized_x, normalized_y, mask, radius, source_context),
        feather * 0.75);
  }

  if (blur > 1e-6) {
    const auto radius = raster_morphology_radius(blur, mask);
    raw_value = std::lerp(
        raw_value,
        local_edge_aware_raw_matte_average(normalized_x, normalized_y, mask, radius, source_context),
        blur);
  }

  if (smoothing > 1e-6) {
    const auto radius = std::min(raster_morphology_radius(smoothing, mask), 12);
    auto sum = 0.0;
    auto total_weight = 0.0;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
      for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
        const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
        if (distance > static_cast<double>(radius) + 1e-6) {
          continue;
        }
        const auto sample_weight = 1.0 / (1.0 + distance);
        const auto sample = raw_segmentation_mask_value(
            normalized_x + static_cast<double>(offset_x) * step_x,
            normalized_y + static_cast<double>(offset_y) * step_y,
            mask);
        sum += sample * sample_weight;
        total_weight += sample_weight;
      }
    }
    if (total_weight > 1e-6) {
      raw_value = std::lerp(raw_value, sum / total_weight, smoothing);
    }
  }

  if (harden > 1e-6) {
    const auto ramp = std::lerp(0.45, 0.04, harden);
    raw_value = smoothstep(0.5 - ramp, 0.5 + ramp, raw_value);
  }

  if (applied != nullptr) {
    *applied = true;
  }
  return raster_selection_weight_from_raw_value(raw_value, mask);
}

[[nodiscard]] double apply_edge_aware_matte(
    double base_weight,
    double normalized_x,
    double normalized_y,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context) {
  const auto refinement = clamp_unit(mask.selection_fine_edge / 100.0);
  const auto weight = clamp_unit(base_weight);
  if (refinement <= 1e-6 || weight <= 1e-6 || weight >= 1.0 - 1e-6 ||
      source_context == nullptr || source_context->pixels == nullptr ||
      source_context->width <= 0 || source_context->height <= 0 ||
      !mask_supports_edge_aware_matte(mask)) {
    return weight;
  }

  const auto expected_mask_size =
      static_cast<std::size_t>(mask.segmentation_width) * static_cast<std::size_t>(mask.segmentation_height);
  if (mask.segmentation_mask.empty() || mask.segmentation_width <= 0 || mask.segmentation_height <= 0 ||
      mask.segmentation_mask.size() != expected_mask_size) {
    return weight;
  }

  const auto source_step_x =
      source_context->width > 1 ? 1.0 / static_cast<double>(source_context->width - 1) : 1.0;
  const auto source_step_y =
      source_context->height > 1 ? 1.0 / static_cast<double>(source_context->height - 1) : 1.0;
  const auto matte_step_x =
      mask.segmentation_width > 1 ? 1.0 / static_cast<double>(mask.segmentation_width - 1) : source_step_x;
  const auto matte_step_y =
      mask.segmentation_height > 1 ? 1.0 / static_cast<double>(mask.segmentation_height - 1) : source_step_y;
  const auto step_x = std::max(source_step_x, matte_step_x);
  const auto step_y = std::max(source_step_y, matte_step_y);

  auto foreground = ColorPixel{};
  auto background = ColorPixel{};
  auto foreground_weight = 0.0;
  auto background_weight = 0.0;
  auto minimum_matte = 1.0;
  auto maximum_matte = 0.0;
  constexpr int kRadius = 2;
  for (int offset_y = -kRadius; offset_y <= kRadius; ++offset_y) {
    for (int offset_x = -kRadius; offset_x <= kRadius; ++offset_x) {
      const auto distance = std::hypot(static_cast<double>(offset_x), static_cast<double>(offset_y));
      if (distance > static_cast<double>(kRadius) + 1e-6) {
        continue;
      }
      const auto sample_x = normalized_x + static_cast<double>(offset_x) * step_x;
      const auto sample_y = normalized_y + static_cast<double>(offset_y) * step_y;
      const auto matte = raw_segmentation_mask_value(sample_x, sample_y, mask);
      minimum_matte = std::min(minimum_matte, matte);
      maximum_matte = std::max(maximum_matte, matte);

      if (matte > 0.70) {
        const auto influence = (matte - 0.70) / 0.30 / (1.0 + distance);
        const auto sample = sample_edge_matte_source(sample_x, sample_y, *source_context);
        foreground.red += static_cast<float>(static_cast<double>(sample.red) * influence);
        foreground.green += static_cast<float>(static_cast<double>(sample.green) * influence);
        foreground.blue += static_cast<float>(static_cast<double>(sample.blue) * influence);
        foreground_weight += influence;
      } else if (matte < 0.30) {
        const auto influence = (0.30 - matte) / 0.30 / (1.0 + distance);
        const auto sample = sample_edge_matte_source(sample_x, sample_y, *source_context);
        background.red += static_cast<float>(static_cast<double>(sample.red) * influence);
        background.green += static_cast<float>(static_cast<double>(sample.green) * influence);
        background.blue += static_cast<float>(static_cast<double>(sample.blue) * influence);
        background_weight += influence;
      }
    }
  }

  const auto matte_edge = maximum_matte - minimum_matte;
  if (foreground_weight <= 1e-6 || background_weight <= 1e-6 || matte_edge <= 0.08) {
    return weight;
  }

  foreground.red = static_cast<float>(static_cast<double>(foreground.red) / foreground_weight);
  foreground.green = static_cast<float>(static_cast<double>(foreground.green) / foreground_weight);
  foreground.blue = static_cast<float>(static_cast<double>(foreground.blue) / foreground_weight);
  background.red = static_cast<float>(static_cast<double>(background.red) / background_weight);
  background.green = static_cast<float>(static_cast<double>(background.green) / background_weight);
  background.blue = static_cast<float>(static_cast<double>(background.blue) / background_weight);

  const auto current = sample_edge_matte_source(normalized_x, normalized_y, *source_context);
  const auto foreground_distance = edge_matte_color_distance(current, foreground);
  const auto background_distance = edge_matte_color_distance(current, background);
  const auto separation = edge_matte_color_distance(foreground, background);
  if (separation <= 1e-5) {
    return weight;
  }

  const auto affinity = clamp_unit(background_distance / (foreground_distance + background_distance + 1e-6));
  const auto ambiguous_band = 1.0 - std::abs((weight * 2.0) - 1.0);
  const auto separation_gate = smoothstep(0.015, 0.18, separation);
  const auto blend = clamp_unit(refinement * matte_edge * ambiguous_band * separation_gate * 0.92);
  return clamp_unit(std::lerp(weight, affinity, blend));
}

[[nodiscard]] double local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask,
    const EdgeAwareMatteContext* source_context = nullptr) {
  if (!mask.enabled) {
    return 0.0;
  }
  const auto refined_weight = [&](double base_weight) {
    auto spatial_expand_smooth_applied = false;
    auto refined_base = apply_raster_selection_morphology(
        base_weight,
        normalized_x,
        normalized_y,
        mask,
        source_context,
        &spatial_expand_smooth_applied);
    return apply_range_refinement(
        apply_selection_refine(
            apply_edge_aware_matte(refined_base, normalized_x, normalized_y, mask, source_context),
            mask,
            spatial_expand_smooth_applied),
        normalized_x,
        normalized_y,
        pixel,
        context,
        mask);
  };

  if (is_ai_segmentation_mask_kind(mask.kind)) {
    return refined_weight(subject_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "radial" || mask.kind == "ellipse") {
    return refined_weight(radial_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "rectangle") {
    return refined_weight(rectangle_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "brush") {
    return refined_weight(brush_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "polygon" || mask.kind == "lasso") {
    return refined_weight(polygon_mask_weight(normalized_x, normalized_y, mask));
  }

  if (mask.kind == "luminance") {
    return refined_weight(luminance_range_mask_weight(pixel, context, mask));
  }

  if (mask.kind == "color") {
    return refined_weight(color_range_mask_weight(pixel, context, mask));
  }

  if (mask.kind == "depth") {
    return refined_weight(depth_range_mask_weight(normalized_x, normalized_y, pixel, context, mask));
  }

  return refined_weight(linear_mask_weight(normalized_x, normalized_y, mask.position, mask.feather, mask.rotation));
}

[[nodiscard]] double nested_local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMask& mask,
    const std::vector<LocalMask>& siblings,
    const EdgeAwareMatteContext* source_context = nullptr) {
  auto weight = local_mask_weight(normalized_x, normalized_y, pixel, context, mask, source_context);
  if (mask.parent_id.empty() || weight <= 0.0) {
    return weight;
  }

  for (const auto& parent : siblings) {
    if (parent.id != mask.parent_id || parent.id == mask.id) {
      continue;
    }
    weight *= local_mask_weight(normalized_x, normalized_y, pixel, context, parent, source_context);
    break;
  }
  return clamp_unit(weight);
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    std::string_view blend_mode,
    double* composite_weight,
    bool* initialized);

[[nodiscard]] std::uint64_t matte_hash_append(std::uint64_t hash, std::uint64_t value) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFULL;
    hash *= kPrime;
  }
  return hash;
}

[[nodiscard]] std::uint64_t matte_hash_append_string(std::uint64_t hash, std::string_view value) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (const auto character : value) {
    hash ^= static_cast<unsigned char>(character);
    hash *= kPrime;
  }
  return matte_hash_append(hash, static_cast<std::uint64_t>(value.size()));
}

[[nodiscard]] bool local_mask_set_has_adjustments(const LocalMaskSet& mask_set) {
  return std::abs(mask_set.exposure) > 1e-6 ||
         std::abs(mask_set.contrast) > 1e-6 ||
         std::abs(mask_set.highlights) > 1e-6 ||
         std::abs(mask_set.shadows) > 1e-6 ||
         std::abs(mask_set.whites) > 1e-6 ||
         std::abs(mask_set.blacks) > 1e-6 ||
         std::abs(mask_set.temperature) > 1e-6 ||
         std::abs(mask_set.tint) > 1e-6 ||
         std::abs(mask_set.vibrance) > 1e-6 ||
         std::abs(mask_set.saturation) > 1e-6 ||
         std::abs(mask_set.hue_shift) > 1e-6 ||
         std::abs(mask_set.saturation_shift) > 1e-6 ||
         std::abs(mask_set.colorize_hue) > 1e-6 ||
         std::abs(mask_set.colorize_strength) > 1e-6;
}

[[nodiscard]] double apply_mask_set_post_composition_refine(
    double base_weight,
    const LocalMaskSet& mask_set) {
  LocalMask refinement;
  refinement.selection_invert = mask_set.selection_invert;
  refinement.selection_expand = mask_set.selection_expand;
  refinement.selection_smooth = mask_set.selection_smooth;
  refinement.selection_fine_edge = mask_set.selection_fine_edge;
  refinement.selection_edge_contrast = mask_set.selection_edge_contrast;
  refinement.opacity = mask_set.opacity;
  refinement.density = mask_set.density;
  return apply_selection_refine(base_weight, refinement);
}

[[nodiscard]] double local_mask_set_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const LocalMaskSet& mask_set,
    const EdgeAwareMatteContext* source_context = nullptr) {
  if (!mask_set.enabled) {
    return 0.0;
  }

  auto composite_weight = 0.0;
  auto initialized = false;
  for (const auto& source : mask_set.sources) {
    if (!source.enabled) {
      continue;
    }

    const auto source_weight_after_source_refinement =
        nested_local_mask_weight(
            normalized_x, normalized_y, pixel, context, source, mask_set.sources, source_context);
    accumulate_composite_weight(
        source_weight_after_source_refinement,
        source.operation,
        source.blend_mode,
        &composite_weight,
        &initialized);
  }

  return initialized ? apply_mask_set_post_composition_refine(composite_weight, mask_set) : 0.0;
}

// A selection-only mask has no local adjustments and no retouch mode.
// It acts purely as a selection modifier (add/subtract/intersect) for other
// masks in the stack, so it should not prevent the composite rendering path.
[[nodiscard]] bool is_selection_only_mask(const LocalMask& mask) {
  return mask.retouch_mode.empty() &&
         std::abs(mask.exposure) <= 1e-6 &&
         std::abs(mask.contrast) <= 1e-6 &&
         std::abs(mask.highlights) <= 1e-6 &&
         std::abs(mask.shadows) <= 1e-6 &&
         std::abs(mask.whites) <= 1e-6 &&
         std::abs(mask.blacks) <= 1e-6 &&
         std::abs(mask.temperature) <= 1e-6 &&
         std::abs(mask.tint) <= 1e-6 &&
         std::abs(mask.vibrance) <= 1e-6 &&
         std::abs(mask.saturation) <= 1e-6 &&
         std::abs(mask.hue_shift) <= 1e-6 &&
         std::abs(mask.saturation_shift) <= 1e-6 &&
         std::abs(mask.colorize_hue) <= 1e-6 &&
         std::abs(mask.colorize_strength) <= 1e-6 &&
         std::abs(mask.remove_strength) <= 1e-6;
}

[[nodiscard]] bool has_selection_decontaminate(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    if (std::clamp(mask.selection_decontaminate, 0.0, 100.0) > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_selection_decontaminate(const LocalMaskSet& mask_set) {
  if (!mask_set.enabled) {
    return false;
  }
  for (const auto& source : mask_set.sources) {
    if (!source.enabled) {
      continue;
    }
    if (std::clamp(source.selection_decontaminate, 0.0, 100.0) > 1e-6) {
      return true;
    }
  }
  return false;
}

void apply_selection_decontaminate_stages(
    ColorPixel* pixel,
    double normalized_x,
    double normalized_y,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    const EdgeAwareMatteContext* source_context = nullptr) {
  if (pixel == nullptr) {
    return;
  }

  const auto weight_pixel = *pixel;
  for (const auto& mask : masks) {
    const auto decontaminate = std::clamp(mask.selection_decontaminate, 0.0, 100.0);
    if (!mask.enabled || decontaminate <= 1e-6) {
      continue;
    }
    const auto mask_weight = nested_local_mask_weight(
        normalized_x,
        normalized_y,
        weight_pixel,
        context,
        mask,
        masks,
        source_context);
    apply_mask_edge_cleanup(pixel, context, mask_weight, decontaminate);
  }
}

void apply_selection_decontaminate_stages(
    ColorPixel* pixel,
    double normalized_x,
    double normalized_y,
    const PipelineColorContext& context,
    const LocalMaskSet& mask_set,
    const EdgeAwareMatteContext* source_context = nullptr) {
  if (pixel == nullptr || !mask_set.enabled) {
    return;
  }
  apply_selection_decontaminate_stages(
      pixel,
      normalized_x,
      normalized_y,
      context,
      mask_set.sources,
      source_context);
}

void accumulate_composite_weight(
    double raw_weight,
    std::string_view operation,
    std::string_view blend_mode,
    double* composite_weight,
    bool* initialized) {
  if (composite_weight == nullptr || initialized == nullptr) {
    return;
  }

  if (operation == "subtract") {
    if (!*initialized) {
      return;
    }
    *composite_weight = std::max(0.0, *composite_weight - raw_weight);
    return;
  }

  if (operation == "intersect") {
    if (!*initialized) {
      return;
    }
    *composite_weight *= raw_weight;
    return;
  }

  if (operation == "exclude") {
    if (!*initialized) {
      *composite_weight = raw_weight;
      *initialized = true;
      return;
    }
    *composite_weight = clamp_unit(*composite_weight + raw_weight - (2.0 * *composite_weight * raw_weight));
    return;
  }

  if (operation == "replace") {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  if (!*initialized) {
    *composite_weight = raw_weight;
    *initialized = true;
    return;
  }

  if (blend_mode == "multiply") {
    *composite_weight *= raw_weight;
  } else if (blend_mode == "overlay") {
    *composite_weight = *composite_weight < 0.5
                            ? 2.0 * *composite_weight * raw_weight
                            : 1.0 - (2.0 * (1.0 - *composite_weight) * (1.0 - raw_weight));
  } else if (blend_mode == "lighten") {
    *composite_weight = std::max(*composite_weight, raw_weight);
  } else if (blend_mode == "darken") {
    *composite_weight = std::min(*composite_weight, raw_weight);
  } else if (blend_mode == "replace") {
    *composite_weight = raw_weight;
  } else {
    *composite_weight = 1.0 - (1.0 - *composite_weight) * (1.0 - raw_weight);
  }
}

[[nodiscard]] bool local_masks_share_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* shared_adjustments) {
  auto found_adjustment = false;
  LocalAdjustmentSettings adjustments;
  auto selection_initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto seeds_selection = mask.operation != "subtract" && mask.operation != "intersect";
    const auto can_contribute_adjustments = selection_initialized || seeds_selection;
    selection_initialized = selection_initialized || seeds_selection;
    if (!can_contribute_adjustments) {
      continue;
    }

    // Skip selection-only masks — they refine the composite selection area
    // without contributing their own adjustments, so they should not cause
    // the renderer to fall back to the independent per-mask path.
    if (is_selection_only_mask(mask)) {
      continue;
    }

    if (!found_adjustment) {
      adjustments.exposure = mask.exposure;
      adjustments.contrast = mask.contrast;
      adjustments.highlights = mask.highlights;
      adjustments.shadows = mask.shadows;
      adjustments.whites = mask.whites;
      adjustments.blacks = mask.blacks;
      adjustments.temperature = mask.temperature;
      adjustments.tint = mask.tint;
      adjustments.vibrance = mask.vibrance;
      adjustments.saturation = mask.saturation;
      adjustments.hue_shift = mask.hue_shift;
      adjustments.saturation_shift = mask.saturation_shift;
      adjustments.colorize_hue = mask.colorize_hue;
      adjustments.colorize_strength = mask.colorize_strength;
      adjustments.remove_strength = mask.remove_strength;
      found_adjustment = true;
      continue;
    }

    if (std::abs(mask.exposure - adjustments.exposure) > 1e-6 ||
        std::abs(mask.contrast - adjustments.contrast) > 1e-6 ||
        std::abs(mask.highlights - adjustments.highlights) > 1e-6 ||
        std::abs(mask.shadows - adjustments.shadows) > 1e-6 ||
        std::abs(mask.whites - adjustments.whites) > 1e-6 ||
        std::abs(mask.blacks - adjustments.blacks) > 1e-6 ||
        std::abs(mask.temperature - adjustments.temperature) > 1e-6 ||
        std::abs(mask.tint - adjustments.tint) > 1e-6 ||
        std::abs(mask.vibrance - adjustments.vibrance) > 1e-6 ||
        std::abs(mask.saturation - adjustments.saturation) > 1e-6 ||
        std::abs(mask.hue_shift - adjustments.hue_shift) > 1e-6 ||
        std::abs(mask.saturation_shift - adjustments.saturation_shift) > 1e-6 ||
        std::abs(mask.colorize_hue - adjustments.colorize_hue) > 1e-6 ||
        std::abs(mask.colorize_strength - adjustments.colorize_strength) > 1e-6 ||
        std::abs(mask.remove_strength - adjustments.remove_strength) > 1e-6) {
      return false;
    }
  }

  if (!found_adjustment) {
    return false;
  }

  if (shared_adjustments != nullptr) {
    *shared_adjustments = adjustments;
  }
  return true;
}

[[nodiscard]] bool local_masks_all_additive(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    if (mask.operation != "add" && mask.operation != "union") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool primary_local_mask_adjustments(
    const std::vector<LocalMask>& masks,
    LocalAdjustmentSettings* adjustments) {
  auto selection_initialized = false;
  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }
    const auto seeds_selection = mask.operation != "subtract" && mask.operation != "intersect";
    const auto can_contribute_adjustments = selection_initialized || seeds_selection;
    selection_initialized = selection_initialized || seeds_selection;
    if (!can_contribute_adjustments) {
      continue;
    }
    // Skip selection-only masks so that the primary adjustments come from the
    // first mask that actually has values (e.g. a subject or radial mask),
    // not from a zero-adjustment refinement brush.
    if (is_selection_only_mask(mask)) {
      continue;
    }
    if (adjustments != nullptr) {
      adjustments->exposure = mask.exposure;
      adjustments->contrast = mask.contrast;
      adjustments->highlights = mask.highlights;
      adjustments->shadows = mask.shadows;
      adjustments->whites = mask.whites;
      adjustments->blacks = mask.blacks;
      adjustments->temperature = mask.temperature;
      adjustments->tint = mask.tint;
      adjustments->vibrance = mask.vibrance;
      adjustments->saturation = mask.saturation;
      adjustments->hue_shift = mask.hue_shift;
      adjustments->saturation_shift = mask.saturation_shift;
      adjustments->colorize_hue = mask.colorize_hue;
      adjustments->colorize_strength = mask.colorize_strength;
      adjustments->remove_strength = mask.remove_strength;
    }
    return true;
  }
  return false;
}

[[nodiscard]] bool has_local_mask_removal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.remove_strength <= 1e-6 ||
        !mask.inpaint_patch_pixels.empty()) {
      continue;
    }
    if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone" ||
        mask.retouch_mode == "red-eye" || mask.retouch_mode == "skin-cleanup" ||
        mask.retouch_mode == "frequency-smoothing") {
      continue;
    }
    if (mask.enabled && mask.remove_strength > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_heal(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "heal" &&
        mask.inpaint_patch_pixels.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_clone(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add") {
      continue;
    }
    if (mask.retouch_mode == "clone" &&
        mask.clone_source_set &&
        !mask.brush_points.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_red_eye(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "red-eye") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_skin_cleanup(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "skin-cleanup") {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_local_mask_frequency_smoothing(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (mask.enabled && mask.remove_strength > 1e-6 && mask.retouch_mode == "frequency-smoothing") {
      return true;
    }
  }
  return false;
}

struct HealComposite final {
  double weight = 0.0;
  double texture_blend = 0.68;
  double color_blend = 0.46;
};

[[nodiscard]] double composite_local_removal_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    std::string_view retouch_mode) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    if (!mask.inpaint_patch_pixels.empty()) {
      continue;
    }

    if (!retouch_mode.empty()) {
      if (mask.retouch_mode != retouch_mode) {
        continue;
      }
    } else {
      if (mask.retouch_mode == "heal" || mask.retouch_mode == "clone" ||
          mask.retouch_mode == "red-eye" || mask.retouch_mode == "skin-cleanup" ||
          mask.retouch_mode == "frequency-smoothing") {
        continue;
      }
    }

    const auto strength = clamp_unit(mask.remove_strength / 100.0);
    if (strength <= 1e-6) {
      continue;
    }

    const auto weighted =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks) * strength;
    accumulate_composite_weight(
        weighted, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

[[nodiscard]] HealComposite composite_local_heal(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  auto composite_weight = 0.0;
  auto initialized = false;
  auto texture_sum = 0.0;
  auto color_sum = 0.0;
  auto control_weight_sum = 0.0;

  for (const auto& mask : masks) {
    if (!mask.enabled || !mask.inpaint_patch_pixels.empty() || mask.retouch_mode != "heal") {
      continue;
    }

    const auto strength = clamp_unit(mask.remove_strength / 100.0);
    if (strength <= 1e-6) {
      continue;
    }

    const auto weighted =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks) * strength;
    if (weighted > 1e-6) {
      texture_sum += weighted * clamp_unit(mask.heal_texture_blend);
      color_sum += weighted * clamp_unit(mask.heal_color_blend);
      control_weight_sum += weighted;
    }
    accumulate_composite_weight(
        weighted, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  if (!initialized) {
    return {};
  }
  return HealComposite{
      .weight = clamp_unit(composite_weight),
      .texture_blend = control_weight_sum > 1e-6 ? texture_sum / control_weight_sum : 0.68,
      .color_blend = control_weight_sum > 1e-6 ? color_sum / control_weight_sum : 0.46,
  };
}

struct RemovalSample final {
  ColorPixel pixel;
  double confidence = 0.0;
  int distance = 0;
  bool valid = false;
};

struct RemovalCandidate final {
  ColorPixel pixel;
  double confidence = 0.0;
  double score = 0.0;
  bool valid = false;
};

[[nodiscard]] std::vector<double> grow_mask_weights(
    const std::vector<double>& mask,
    int width,
    int height,
    int radius) {
  if (radius <= 0 || width <= 0 || height <= 0 ||
      mask.size() != static_cast<std::size_t>(width * height)) {
    return mask;
  }

  auto grown = mask;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto maximum = clamp_unit(mask[static_cast<std::size_t>(y * width + x)]);
      for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= height) {
          continue;
        }
        for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
          const auto sample_x = x + offset_x;
          if (sample_x < 0 || sample_x >= width) {
            continue;
          }
          if (offset_x * offset_x + offset_y * offset_y > radius * radius) {
            continue;
          }
          maximum = std::max(
              maximum,
              clamp_unit(mask[static_cast<std::size_t>(sample_y * width + sample_x)]));
        }
      }
      grown[static_cast<std::size_t>(y * width + x)] = maximum;
    }
  }
  return grown;
}

[[nodiscard]] ColorPixel separated_heal_pixel(
    const ColorPixel& source,
    const ColorPixel& source_low,
    const ColorPixel& fill,
    const ColorPixel& fill_low,
    double texture_blend,
    double color_blend) {
  const auto clamped_texture = clamp_unit(texture_blend);
  const auto clamped_color = clamp_unit(color_blend);
  const auto channel = [&](float source_low_channel,
                           float fill_channel,
                           float fill_low_channel) {
    const auto low_frequency =
        std::lerp(static_cast<double>(source_low_channel), static_cast<double>(fill_low_channel), clamped_color);
    const auto detail =
        (static_cast<double>(fill_channel) - static_cast<double>(fill_low_channel)) * clamped_texture;
    return static_cast<float>(std::clamp(low_frequency + detail, 0.0, 1.0));
  };
  return ColorPixel{
      .red = channel(source_low.red, fill.red, fill_low.red),
      .green = channel(source_low.green, fill.green, fill_low.green),
      .blue = channel(source_low.blue, fill.blue, fill_low.blue),
      .alpha = source.alpha,
  };
}

[[nodiscard]] std::vector<ColorPixel> low_frequency_pixels(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {}) {
  const auto pixel_count = pixels.size();
  std::vector<float> red(pixel_count, 0.0F);
  std::vector<float> green(pixel_count, 0.0F);
  std::vector<float> blue(pixel_count, 0.0F);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return {};
    }
    red[index] = pixels[index].red;
    green[index] = pixels[index].green;
    blue[index] = pixels[index].blue;
  }
  const auto blurred_red = box_blur(red, width, height, radius, cancellation);
  const auto blurred_green = box_blur(green, width, height, radius, cancellation);
  const auto blurred_blue = box_blur(blue, width, height, radius, cancellation);
  if (cancellation.cancelled() || blurred_red.size() != pixel_count ||
      blurred_green.size() != pixel_count || blurred_blue.size() != pixel_count) {
    return {};
  }
  std::vector<ColorPixel> low(pixel_count);
  for (std::size_t index = 0; index < pixel_count; ++index) {
    low[index] = ColorPixel{
        .red = blurred_red[index],
        .green = blurred_green[index],
        .blue = blurred_blue[index],
        .alpha = pixels[index].alpha,
    };
  }
  return low;
}

[[nodiscard]] double removal_color_distance(const ColorPixel& left, const ColorPixel& right) {
  const auto dr = static_cast<double>(left.red) - static_cast<double>(right.red);
  const auto dg = static_cast<double>(left.green) - static_cast<double>(right.green);
  const auto db = static_cast<double>(left.blue) - static_cast<double>(right.blue);
  return std::sqrt(dr * dr + dg * dg + db * db);
}

[[nodiscard]] RemovalSample nearest_known_removal_sample(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int start_x,
    int start_y,
    int step_x,
    int step_y,
    int max_distance) {
  RemovalSample sample;
  if (width <= 0 || height <= 0 || start_x < 0 || start_y < 0 ||
      start_x >= width || start_y >= height || max_distance <= 0) {
    return sample;
  }

  for (int distance = 1; distance <= max_distance; ++distance) {
    const auto x = start_x + step_x * distance;
    const auto y = start_y + step_y * distance;
    if (x < 0 || y < 0 || x >= width || y >= height) {
      break;
    }
    const auto index = static_cast<std::size_t>(y * width + x);
    if (confidence[index] <= 1e-4F) {
      continue;
    }
    sample.pixel = pixels[index];
    sample.confidence = static_cast<double>(confidence[index]);
    sample.distance = distance;
    sample.valid = true;
    return sample;
  }

  return sample;
}

[[nodiscard]] bool has_known_neighbor(
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y) {
  for (int offset_y = -1; offset_y <= 1; ++offset_y) {
    for (int offset_x = -1; offset_x <= 1; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      if (confidence[static_cast<std::size_t>(sample_y * width + sample_x)] > 1e-4F) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] RemovalCandidate directional_removal_candidate(
    const std::vector<ColorPixel>& pixels,
    const std::vector<float>& confidence,
    int width,
    int height,
    int x,
    int y,
    int max_line_radius) {
  RemovalCandidate best_candidate;
  const auto sample_structure_strength =
      [&](int sample_x, int sample_y, int step_x, int step_y) {
        if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
          return 0.0;
        }
        const auto sample_index = static_cast<std::size_t>(sample_y * width + sample_x);
        if (confidence[sample_index] <= 1e-4F) {
          return 0.0;
        }
        const auto perp_x = -step_y;
        const auto perp_y = step_x;
        auto structure = 0.0;
        auto count = 0.0;
        for (const auto sign : {-1, 1}) {
          const auto neighbor_x = sample_x + perp_x * sign;
          const auto neighbor_y = sample_y + perp_y * sign;
          if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= width || neighbor_y >= height) {
            continue;
          }
          const auto neighbor_index = static_cast<std::size_t>(neighbor_y * width + neighbor_x);
          if (confidence[neighbor_index] <= 1e-4F) {
            continue;
          }
          structure += removal_color_distance(pixels[sample_index], pixels[neighbor_index]);
          count += 1.0;
        }
        return count > 0.0 ? structure / count : 0.0;
      };
  constexpr std::array<std::pair<int, int>, 4> kDirections{
      std::pair<int, int>{1, 0},
      std::pair<int, int>{0, 1},
      std::pair<int, int>{1, 1},
      std::pair<int, int>{1, -1},
  };

  for (const auto& [step_x, step_y] : kDirections) {
    const auto forward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, step_x, step_y, max_line_radius);
    const auto backward = nearest_known_removal_sample(
        pixels, confidence, width, height, x, y, -step_x, -step_y, max_line_radius);
    if (!forward.valid && !backward.valid) {
      continue;
    }

    RemovalCandidate candidate;
    if (forward.valid && backward.valid) {
      const auto forward_weight = forward.confidence / static_cast<double>(std::max(1, forward.distance));
      const auto backward_weight = backward.confidence / static_cast<double>(std::max(1, backward.distance));
      const auto weight_sum = std::max(1e-6, forward_weight + backward_weight);
      candidate.pixel = ColorPixel{
          .red = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.red) * forward_weight +
               static_cast<double>(backward.pixel.red) * backward_weight) /
                  weight_sum)),
          .green = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.green) * forward_weight +
               static_cast<double>(backward.pixel.green) * backward_weight) /
                  weight_sum)),
          .blue = static_cast<float>(std::max(
              0.0,
              (static_cast<double>(forward.pixel.blue) * forward_weight +
               static_cast<double>(backward.pixel.blue) * backward_weight) /
                  weight_sum)),
          .alpha = 1.0F,
      };
      const auto color_match =
          1.0 / (1.0 + removal_color_distance(forward.pixel, backward.pixel) * 6.0);
      const auto structure_strength =
          sample_structure_strength(x + step_x * forward.distance, y + step_y * forward.distance, step_x, step_y) +
          sample_structure_strength(x - step_x * backward.distance, y - step_y * backward.distance, step_x, step_y);
      const auto distance_sum = static_cast<double>(forward.distance + backward.distance);
      candidate.confidence = std::clamp(
          0.45 + 0.25 * std::min(forward.confidence, backward.confidence) + 0.20 * color_match +
              std::min(0.10, structure_strength * 0.10),
          0.0, 1.0);
      candidate.score =
          (forward_weight + backward_weight) * color_match *
          (1.0 + std::min(1.5, structure_strength * 4.0)) /
          std::max(1.0, distance_sum * 0.5);
    } else {
      const auto& sample = forward.valid ? forward : backward;
      candidate.pixel = sample.pixel;
      candidate.confidence =
          std::clamp(0.28 + 0.45 * sample.confidence, 0.0, 0.9);
      candidate.score =
          (sample.confidence / static_cast<double>(std::max(1, sample.distance))) * 0.35;
    }
    candidate.pixel.alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha;
    candidate.valid = true;
    if (!best_candidate.valid || candidate.score > best_candidate.score) {
      best_candidate = candidate;
    }
  }

  RemovalCandidate neighborhood_candidate;
  double weighted_red = 0.0;
  double weighted_green = 0.0;
  double weighted_blue = 0.0;
  double total_weight = 0.0;
  double confidence_sum = 0.0;
  for (int offset_y = -2; offset_y <= 2; ++offset_y) {
    for (int offset_x = -2; offset_x <= 2; ++offset_x) {
      if (offset_x == 0 && offset_y == 0) {
        continue;
      }
      const auto sample_x = x + offset_x;
      const auto sample_y = y + offset_y;
      if (sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
        continue;
      }
      const auto index = static_cast<std::size_t>(sample_y * width + sample_x);
      const auto sample_confidence = static_cast<double>(confidence[index]);
      if (sample_confidence <= 1e-4) {
        continue;
      }
      const auto distance_sq = static_cast<double>(offset_x * offset_x + offset_y * offset_y);
      const auto weight = sample_confidence / std::max(1.0, distance_sq);
      weighted_red += static_cast<double>(pixels[index].red) * weight;
      weighted_green += static_cast<double>(pixels[index].green) * weight;
      weighted_blue += static_cast<double>(pixels[index].blue) * weight;
      total_weight += weight;
      confidence_sum += sample_confidence;
    }
  }
  if (total_weight > 1e-6) {
    neighborhood_candidate.pixel = ColorPixel{
        .red = static_cast<float>(std::max(0.0, weighted_red / total_weight)),
        .green = static_cast<float>(std::max(0.0, weighted_green / total_weight)),
        .blue = static_cast<float>(std::max(0.0, weighted_blue / total_weight)),
        .alpha = pixels[static_cast<std::size_t>(y * width + x)].alpha,
    };
    neighborhood_candidate.confidence =
        std::clamp(0.20 + confidence_sum / (total_weight * 8.0), 0.0, 0.85);
    neighborhood_candidate.score = total_weight * 0.06;
    neighborhood_candidate.valid = true;
  }

  if (!best_candidate.valid ||
      (neighborhood_candidate.valid && neighborhood_candidate.score > best_candidate.score)) {
    return neighborhood_candidate;
  }
  return best_candidate;
}

void apply_local_removal(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const std::vector<double>& removal_mask,
    double detail_preservation,
    RemovalQuality removal_quality,
    double texture_blend = 1.0,
    double color_blend = 1.0,
    CancellationToken cancellation = {}) {
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      pixels->size() != removal_mask.size()) {
    return;
  }
  if (cancellation.cancelled()) {
    return;
  }

  const auto pixel_count = pixels->size();
  const auto source_exclusion_radius =
      removal_quality == RemovalQuality::Final
          ? (detail_preservation >= 0.45 ? 1 : 2)
          : 0;
  const auto working_mask =
      source_exclusion_radius > 0
          ? grow_mask_weights(removal_mask, width, height, source_exclusion_radius)
          : removal_mask;
  auto filled_pixels = *pixels;
  std::vector<float> confidence(pixel_count, 1.0F);
  std::vector<uint8_t> unresolved(pixel_count, 0);
  auto has_masked_pixels = false;
  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return;
    }
    confidence[index] = static_cast<float>(1.0 - clamp_unit(working_mask[index]));
    has_masked_pixels = has_masked_pixels || working_mask[index] > 1e-4;
    unresolved[index] = working_mask[index] > 1e-4 ? 1U : 0U;
  }
  if (!has_masked_pixels) {
    return;
  }

  const auto max_line_radius =
      std::clamp(std::min(width, height) / 10, 3, 24);
  auto unresolved_count = 0;
  for (const auto flag : unresolved) {
    unresolved_count += flag != 0 ? 1 : 0;
  }

  while (unresolved_count > 0) {
    if (cancellation.cancelled()) {
      return;
    }
    auto next_pixels = filled_pixels;
    auto next_confidence = confidence;
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(static_cast<std::size_t>(unresolved_count));

    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (unresolved[index] == 0U || !has_known_neighbor(confidence, width, height, x, y)) {
          continue;
        }

        const auto candidate = directional_removal_candidate(
            filled_pixels,
            confidence,
            width,
            height,
            x,
            y,
            max_line_radius);
        if (!candidate.valid) {
          continue;
        }

        const auto mask_weight = clamp_unit(working_mask[index]);
        const auto blend =
            std::clamp(mask_weight * (0.78 + candidate.confidence * 0.22), 0.0, 1.0);
        next_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.pixel.red), blend));
        next_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.pixel.green), blend));
        next_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.pixel.blue), blend));
        next_confidence[index] = static_cast<float>(std::max(
            static_cast<double>(next_confidence[index]),
            std::clamp(candidate.confidence, 0.0, 1.0)));
        resolved_indices.push_back(index);
      }
    }

    if (resolved_indices.empty()) {
      break;
    }

    filled_pixels = std::move(next_pixels);
    confidence = std::move(next_confidence);
    for (const auto index : resolved_indices) {
      if (unresolved[index] == 0U) {
        continue;
      }
      unresolved[index] = 0U;
      --unresolved_count;
    }
  }

  if (unresolved_count > 0) {
    constexpr std::array<int, 5> kFillRadii{2, 4, 8, 12, 18};
    for (const auto radius : kFillRadii) {
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> weighted_red(pixel_count, 0.0F);
      std::vector<float> weighted_green(pixel_count, 0.0F);
      std::vector<float> weighted_blue(pixel_count, 0.0F);

      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
          return;
        }
        weighted_red[index] = static_cast<float>(filled_pixels[index].red) * confidence[index];
        weighted_green[index] = static_cast<float>(filled_pixels[index].green) * confidence[index];
        weighted_blue[index] = static_cast<float>(filled_pixels[index].blue) * confidence[index];
      }

      const auto blurred_confidence = box_blur(confidence, width, height, radius, cancellation);
      const auto blurred_red = box_blur(weighted_red, width, height, radius, cancellation);
      const auto blurred_green = box_blur(weighted_green, width, height, radius, cancellation);
      const auto blurred_blue = box_blur(weighted_blue, width, height, radius, cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      for (std::size_t index = 0; index < pixel_count; ++index) {
        if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
          return;
        }
        if (unresolved[index] == 0U) {
          continue;
        }
        const auto local_confidence = static_cast<double>(blurred_confidence[index]);
        if (local_confidence <= 1e-5) {
          continue;
        }

        const auto candidate = ColorPixel{
            .red = static_cast<float>(std::max(0.0, static_cast<double>(blurred_red[index]) / local_confidence)),
            .green = static_cast<float>(std::max(0.0, static_cast<double>(blurred_green[index]) / local_confidence)),
            .blue = static_cast<float>(std::max(0.0, static_cast<double>(blurred_blue[index]) / local_confidence)),
            .alpha = filled_pixels[index].alpha,
        };
        const auto blend =
            std::clamp(0.40 + (1.0 - static_cast<double>(confidence[index])) * 0.60, 0.0, 1.0);
        filled_pixels[index].red = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].red), static_cast<double>(candidate.red), blend));
        filled_pixels[index].green = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].green), static_cast<double>(candidate.green), blend));
        filled_pixels[index].blue = static_cast<float>(
            std::lerp(static_cast<double>(filled_pixels[index].blue), static_cast<double>(candidate.blue), blend));
        confidence[index] = static_cast<float>(std::max(
            static_cast<double>(confidence[index]),
            std::clamp(local_confidence, 0.0, 1.0)));
      }
    }
  }

  if (detail_preservation > 1e-6) {
    std::vector<float> filled_red(pixel_count, 0.0F);
    std::vector<float> filled_green(pixel_count, 0.0F);
    std::vector<float> filled_blue(pixel_count, 0.0F);
    for (std::size_t index = 0; index < pixel_count; ++index) {
      if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
        return;
      }
      filled_red[index] = filled_pixels[index].red;
      filled_green[index] = filled_pixels[index].green;
      filled_blue[index] = filled_pixels[index].blue;
    }
    const auto blurred_red = box_blur(filled_red, width, height, 2, cancellation);
    const auto blurred_green = box_blur(filled_green, width, height, 2, cancellation);
    const auto blurred_blue = box_blur(filled_blue, width, height, 2, cancellation);
    if (cancellation.cancelled()) {
      return;
    }
    for (std::size_t index = 0; index < pixel_count; ++index) {
      if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
        return;
      }
      const auto mask_weight = clamp_unit(working_mask[index]);
      if (mask_weight <= 1e-4) {
        continue;
      }
      const auto detail_gain = detail_preservation * mask_weight * 0.30;
      filled_pixels[index].red = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].red) +
              static_cast<double>(filled_red[index] - blurred_red[index]) * detail_gain));
      filled_pixels[index].green = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].green) +
              static_cast<double>(filled_green[index] - blurred_green[index]) * detail_gain));
      filled_pixels[index].blue = static_cast<float>(std::max(
          0.0,
          static_cast<double>(filled_pixels[index].blue) +
              static_cast<double>(filled_blue[index] - blurred_blue[index]) * detail_gain));
    }
  }

  if (removal_quality == RemovalQuality::Final) {
    auto min_mask_x = width;
    auto min_mask_y = height;
    auto max_mask_x = -1;
    auto max_mask_y = -1;
    auto masked_pixel_count = 0;
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        if (clamp_unit(working_mask[index]) <= 1e-4) {
          continue;
        }
        min_mask_x = std::min(min_mask_x, x);
        min_mask_y = std::min(min_mask_y, y);
        max_mask_x = std::max(max_mask_x, x);
        max_mask_y = std::max(max_mask_y, y);
        ++masked_pixel_count;
      }
    }

    if (masked_pixel_count > 0) {
      const auto search_radius = std::clamp(std::max(width, height) / 16, 12, 64);
      const auto min_search_x = std::max(0, min_mask_x - search_radius);
      const auto min_search_y = std::max(0, min_mask_y - search_radius);
      const auto max_search_x = std::min(width - 1, max_mask_x + search_radius);
      const auto max_search_y = std::min(height - 1, max_mask_y + search_radius);

      std::vector<std::size_t> candidate_indices;
      candidate_indices.reserve(
          static_cast<std::size_t>(std::max(0, (max_search_x - min_search_x + 1) *
                                                   (max_search_y - min_search_y + 1))));
      for (int y = min_search_y; y <= max_search_y; ++y) {
        if (cancellation.cancelled()) {
          return;
        }
        for (int x = min_search_x; x <= max_search_x; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          if (clamp_unit(working_mask[index]) > 1e-4) {
            continue;
          }
          candidate_indices.push_back(index);
        }
      }

      if (!candidate_indices.empty()) {
        const auto candidate_stride =
            std::max(1, static_cast<int>(candidate_indices.size() / 4096));
        const auto patch_radius = 2;
        auto refined_pixels = filled_pixels;
        for (int y = min_mask_y; y <= max_mask_y; ++y) {
          if (cancellation.cancelled()) {
            return;
          }
          for (int x = min_mask_x; x <= max_mask_x; ++x) {
            const auto index = static_cast<std::size_t>(y * width + x);
            const auto mask_weight = clamp_unit(working_mask[index]);
            if (mask_weight <= 1e-4) {
              continue;
            }

            auto best_score = std::numeric_limits<double>::max();
            auto best_candidate = refined_pixels[index];
            auto found_candidate = false;
            for (std::size_t candidate_offset = 0;
                 candidate_offset < candidate_indices.size();
                 candidate_offset += static_cast<std::size_t>(candidate_stride)) {
              if ((candidate_offset & 0x3FFU) == 0U && cancellation.cancelled()) {
                return;
              }
              const auto candidate_index = candidate_indices[candidate_offset];
              const auto candidate_x = static_cast<int>(candidate_index % static_cast<std::size_t>(width));
              const auto candidate_y = static_cast<int>(candidate_index / static_cast<std::size_t>(width));

              auto score = 0.0;
              auto support = 0.0;
              for (int offset_y = -patch_radius; offset_y <= patch_radius; ++offset_y) {
                for (int offset_x = -patch_radius; offset_x <= patch_radius; ++offset_x) {
                  if (offset_x == 0 && offset_y == 0) {
                    continue;
                  }
                  const auto target_x = x + offset_x;
                  const auto target_y = y + offset_y;
                  const auto sample_x = candidate_x + offset_x;
                  const auto sample_y = candidate_y + offset_y;
                  if (target_x < 0 || target_y < 0 || target_x >= width || target_y >= height ||
                      sample_x < 0 || sample_y < 0 || sample_x >= width || sample_y >= height) {
                    continue;
                  }

                  const auto target_index =
                      static_cast<std::size_t>(target_y * width + target_x);
                  const auto sample_index =
                      static_cast<std::size_t>(sample_y * width + sample_x);
                  if (clamp_unit(working_mask[sample_index]) > 1e-4) {
                    continue;
                  }

                  const auto context_weight =
                      1.0 / std::max(1.0, static_cast<double>(offset_x * offset_x + offset_y * offset_y));
                  const auto target_context =
                      clamp_unit(working_mask[target_index]) <= 0.35 ? (*pixels)[target_index]
                                                                     : filled_pixels[target_index];
                  score += removal_color_distance(target_context, filled_pixels[sample_index]) *
                           context_weight;
                  support += context_weight;
                }
              }

              if (support <= 1e-6) {
                continue;
              }
              score /= support;
              if (score < best_score) {
                best_score = score;
                best_candidate = filled_pixels[candidate_index];
                found_candidate = true;
              }
            }

            if (!found_candidate) {
              continue;
            }

            const auto blend =
                std::clamp(0.45 + mask_weight * 0.45, 0.0, 1.0);
            refined_pixels[index].red = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].red), static_cast<double>(best_candidate.red), blend));
            refined_pixels[index].green = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].green), static_cast<double>(best_candidate.green), blend));
            refined_pixels[index].blue = static_cast<float>(
                std::lerp(static_cast<double>(refined_pixels[index].blue), static_cast<double>(best_candidate.blue), blend));
          }
        }
        filled_pixels = std::move(refined_pixels);
      }
    }
  }

  const auto use_separated_heal =
      std::abs(clamp_unit(texture_blend) - 1.0) > 1e-6 ||
      std::abs(clamp_unit(color_blend) - 1.0) > 1e-6;
  auto source_low = std::vector<ColorPixel>{};
  auto filled_low = std::vector<ColorPixel>{};
  if (use_separated_heal) {
    source_low = low_frequency_pixels(*pixels, width, height, 2, cancellation);
    filled_low = low_frequency_pixels(filled_pixels, width, height, 2, cancellation);
    if (cancellation.cancelled()) {
      return;
    }
    if (source_low.size() != pixel_count || filled_low.size() != pixel_count) {
      source_low.clear();
      filled_low.clear();
    }
  }

  for (std::size_t index = 0; index < pixel_count; ++index) {
    if ((index & 0xFFFU) == 0U && cancellation.cancelled()) {
      return;
    }
    const auto mask_weight = clamp_unit(removal_mask[index]);
    if (mask_weight <= 1e-4) {
      continue;
    }
    const auto target =
        !source_low.empty() && !filled_low.empty()
            ? separated_heal_pixel(
                  (*pixels)[index],
                  source_low[index],
                  filled_pixels[index],
                  filled_low[index],
                  texture_blend,
                  color_blend)
            : filled_pixels[index];
    (*pixels)[index].red = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(target.red), mask_weight));
    (*pixels)[index].green = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(target.green), mask_weight));
    (*pixels)[index].blue = static_cast<float>(
        std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(target.blue), mask_weight));
  }
}

void apply_local_clone(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (const auto& mask : masks) {
    if (!mask.enabled || mask.kind != "brush" || mask.operation != "add" ||
        mask.retouch_mode != "clone" || !mask.clone_source_set ||
        mask.brush_points.empty()) {
      continue;
    }

    const auto source_pixels = *pixels;
    const auto source_center_x = mask.clone_source_x * static_cast<double>(std::max(width - 1, 0));
    const auto source_center_y = mask.clone_source_y * static_cast<double>(std::max(height - 1, 0));
    const auto offset_x = mask.clone_offset_x * static_cast<double>(std::max(width - 1, 0));
    const auto offset_y = mask.clone_offset_y * static_cast<double>(std::max(height - 1, 0));
    const auto destination_origin_x = source_center_x - offset_x;
    const auto destination_origin_y = source_center_y - offset_y;
    const auto scale = std::clamp(mask.clone_scale, 0.25, 4.0);
    const auto radians = mask.clone_rotation * kPi / 180.0;
    const auto cosine = std::cos(radians);
    const auto sine = std::sin(radians);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        const auto weight = local_mask_weight(normalized_x, normalized_y, source_pixels[index], context, mask);
        if (weight <= 1e-4) {
          continue;
        }

        const auto dx = static_cast<double>(x) - destination_origin_x;
        const auto dy = static_cast<double>(y) - destination_origin_y;
        const auto source_dx = (dx * cosine + dy * sine) / scale;
        const auto source_dy = (-dx * sine + dy * cosine) / scale;
        const auto candidate = sample_bilinear(
            source_pixels,
            width,
            height,
            source_center_x + source_dx,
            source_center_y + source_dy);
        const auto blend = clamp_unit(weight * clamp_unit(mask.brush_flow));
        (*pixels)[index].red = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].red), static_cast<double>(candidate.red), blend));
        (*pixels)[index].green = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].green), static_cast<double>(candidate.green), blend));
        (*pixels)[index].blue = static_cast<float>(
            std::lerp(static_cast<double>((*pixels)[index].blue), static_cast<double>(candidate.blue), blend));
      }
    }
  }
}

void apply_local_red_eye(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, pixel, context, masks, "red-eye");
      if (weight <= 1e-4) {
        continue;
      }

      const auto green_blue_max = std::max(pixel.green, pixel.blue);
      const auto excess_red = std::max(0.0F, pixel.red - green_blue_max * 1.05F);
      if (excess_red <= 1e-5F) {
        continue;
      }
      const auto neutral_red = std::max(0.0F, green_blue_max * 0.92F);
      const auto blend = clamp_unit(weight);
      pixel.red = static_cast<float>(
          std::lerp(static_cast<double>(pixel.red), static_cast<double>(neutral_red), blend));
      const auto lift = static_cast<double>(excess_red) * 0.12 * blend;
      pixel.green = static_cast<float>(std::min(1.0, static_cast<double>(pixel.green) + lift));
      pixel.blue = static_cast<float>(std::min(1.0, static_cast<double>(pixel.blue) + lift));
    }
  }
}

void apply_local_skin_cleanup(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  const auto source_pixels = *pixels;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, source_pixels[index], context, masks, "skin-cleanup");
      if (weight <= 1e-4) {
        continue;
      }

      auto red_sum = 0.0;
      auto green_sum = 0.0;
      auto blue_sum = 0.0;
      auto total_weight = 0.0;
      for (int dy = -2; dy <= 2; ++dy) {
        const auto sample_y = std::clamp(y + dy, 0, height - 1);
        for (int dx = -2; dx <= 2; ++dx) {
          const auto sample_x = std::clamp(x + dx, 0, width - 1);
          const auto distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
          const auto sample_weight = 1.0 / (1.0 + distance);
          const auto& sample = source_pixels[static_cast<std::size_t>(sample_y * width + sample_x)];
          red_sum += static_cast<double>(sample.red) * sample_weight;
          green_sum += static_cast<double>(sample.green) * sample_weight;
          blue_sum += static_cast<double>(sample.blue) * sample_weight;
          total_weight += sample_weight;
        }
      }
      if (total_weight <= 1e-6) {
        continue;
      }

      auto& pixel = (*pixels)[index];
      const auto blend = clamp_unit(weight * 0.65);
      pixel.red = static_cast<float>(
          std::lerp(static_cast<double>(pixel.red), red_sum / total_weight, blend));
      pixel.green = static_cast<float>(
          std::lerp(static_cast<double>(pixel.green), green_sum / total_weight, blend));
      pixel.blue = static_cast<float>(
          std::lerp(static_cast<double>(pixel.blue), blue_sum / total_weight, blend));
    }
  }
}

void apply_local_frequency_smoothing(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  const auto source_pixels = *pixels;
  const auto sample_average = [&](int center_x, int center_y, int radius) {
    auto red_sum = 0.0;
    auto green_sum = 0.0;
    auto blue_sum = 0.0;
    auto total_weight = 0.0;
    for (int dy = -radius; dy <= radius; ++dy) {
      const auto y = std::clamp(center_y + dy, 0, height - 1);
      for (int dx = -radius; dx <= radius; ++dx) {
        const auto x = std::clamp(center_x + dx, 0, width - 1);
        const auto distance = std::hypot(static_cast<double>(dx), static_cast<double>(dy));
        if (distance > static_cast<double>(radius) + 0.25) {
          continue;
        }
        const auto weight = 1.0 / (1.0 + distance);
        const auto& sample = source_pixels[static_cast<std::size_t>(y * width + x)];
        red_sum += static_cast<double>(sample.red) * weight;
        green_sum += static_cast<double>(sample.green) * weight;
        blue_sum += static_cast<double>(sample.blue) * weight;
        total_weight += weight;
      }
    }
    if (total_weight <= 1e-6) {
      return source_pixels[static_cast<std::size_t>(center_y * width + center_x)];
    }
    return ColorPixel{
        .red = static_cast<float>(red_sum / total_weight),
        .green = static_cast<float>(green_sum / total_weight),
        .blue = static_cast<float>(blue_sum / total_weight),
        .alpha = source_pixels[static_cast<std::size_t>(center_y * width + center_x)].alpha,
    };
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      const auto weight = composite_local_removal_weight(
          normalized_x, normalized_y, source_pixels[index], context, masks, "frequency-smoothing");
      if (weight <= 1e-4) {
        continue;
      }

      const auto low_frequency = sample_average(x, y, 4);
      const auto local_base = sample_average(x, y, 1);
      const auto& source = source_pixels[index];
      const auto target = ColorPixel{
          .red = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.red) +
                  (static_cast<double>(source.red) - static_cast<double>(local_base.red)) * 0.35,
              0.0,
              1.0)),
          .green = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.green) +
                  (static_cast<double>(source.green) - static_cast<double>(local_base.green)) * 0.35,
              0.0,
              1.0)),
          .blue = static_cast<float>(std::clamp(
              static_cast<double>(low_frequency.blue) +
                  (static_cast<double>(source.blue) - static_cast<double>(local_base.blue)) * 0.35,
              0.0,
              1.0)),
          .alpha = source.alpha,
      };
      const auto blend = clamp_unit(weight * 0.82);
      auto& pixel = (*pixels)[index];
      pixel.red = static_cast<float>(std::lerp(static_cast<double>(pixel.red), static_cast<double>(target.red), blend));
      pixel.green = static_cast<float>(std::lerp(static_cast<double>(pixel.green), static_cast<double>(target.green), blend));
      pixel.blue = static_cast<float>(std::lerp(static_cast<double>(pixel.blue), static_cast<double>(target.blue), blend));
    }
  }
}

[[nodiscard]] bool inpaint_patch_available(const LocalMask& mask) {
  return mask.enabled && mask.remove_strength > 1e-6 &&
         mask.inpaint_patch_width > 0 && mask.inpaint_patch_height > 0 &&
         mask.inpaint_patch_pixels.size() ==
             static_cast<std::size_t>(mask.inpaint_patch_width * mask.inpaint_patch_height);
}

[[nodiscard]] bool has_local_inpaint_patch(const std::vector<LocalMask>& masks) {
  for (const auto& mask : masks) {
    if (inpaint_patch_available(mask)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] ColorPixel matched_inpaint_patch_pixel(
    const ColorPixel& patch_pixel,
    const ColorPixel& color_delta,
    double color_match) {
  const auto strength = clamp_unit(color_match);
  return ColorPixel{
      .red = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.red) + static_cast<double>(color_delta.red) * strength,
          0.0,
          1.0)),
      .green = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.green) + static_cast<double>(color_delta.green) * strength,
          0.0,
          1.0)),
      .blue = static_cast<float>(std::clamp(
          static_cast<double>(patch_pixel.blue) + static_cast<double>(color_delta.blue) * strength,
          0.0,
          1.0)),
      .alpha = patch_pixel.alpha,
  };
}

[[nodiscard]] ColorPixel inpaint_patch_seam_color_delta(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const LocalMask& mask) {
  ColorPixel delta{};
  auto weight_sum = 0.0;
  const auto add_sample = [&](int patch_x, int patch_y, double sample_weight) {
    const auto target_x = mask.inpaint_patch_x + patch_x;
    const auto target_y = mask.inpaint_patch_y + patch_y;
    if (target_x < 0 || target_y < 0 || target_x >= width || target_y >= height ||
        patch_x < 0 || patch_y < 0 || patch_x >= mask.inpaint_patch_width ||
        patch_y >= mask.inpaint_patch_height) {
      return;
    }

    const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
    const auto patch_index = static_cast<std::size_t>(patch_y * mask.inpaint_patch_width + patch_x);
    const auto& source = pixels[target_index];
    const auto& patch = mask.inpaint_patch_pixels[patch_index];
    delta.red += static_cast<float>((static_cast<double>(source.red) - patch.red) * sample_weight);
    delta.green += static_cast<float>((static_cast<double>(source.green) - patch.green) * sample_weight);
    delta.blue += static_cast<float>((static_cast<double>(source.blue) - patch.blue) * sample_weight);
    weight_sum += sample_weight;
  };

  for (int patch_y = 0; patch_y < mask.inpaint_patch_height; ++patch_y) {
    const auto target_y = mask.inpaint_patch_y + patch_y;
    if (target_y < 0 || target_y >= height) {
      continue;
    }
    for (int patch_x = 0; patch_x < mask.inpaint_patch_width; ++patch_x) {
      const auto target_x = mask.inpaint_patch_x + patch_x;
      if (target_x < 0 || target_x >= width) {
        continue;
      }

      const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(target_x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(target_y) / static_cast<double>(height - 1) : 0.5;
      const auto matte = local_mask_weight(normalized_x, normalized_y, pixels[target_index], context, mask);
      if (matte > 0.02 && matte < 0.98) {
        add_sample(patch_x, patch_y, 1.0 - std::abs(matte - 0.5) * 1.4);
      }
    }
  }

  if (weight_sum <= 1e-6) {
    for (int patch_x = 0; patch_x < mask.inpaint_patch_width; ++patch_x) {
      add_sample(patch_x, 0, 1.0);
      add_sample(patch_x, mask.inpaint_patch_height - 1, 1.0);
    }
    for (int patch_y = 1; patch_y + 1 < mask.inpaint_patch_height; ++patch_y) {
      add_sample(0, patch_y, 1.0);
      add_sample(mask.inpaint_patch_width - 1, patch_y, 1.0);
    }
  }

  if (weight_sum <= 1e-6) {
    return delta;
  }

  delta.red = static_cast<float>(std::clamp(static_cast<double>(delta.red) / weight_sum, -0.35, 0.35));
  delta.green = static_cast<float>(std::clamp(static_cast<double>(delta.green) / weight_sum, -0.35, 0.35));
  delta.blue = static_cast<float>(std::clamp(static_cast<double>(delta.blue) / weight_sum, -0.35, 0.35));
  delta.alpha = 1.0F;
  return delta;
}

[[nodiscard]] double inpaint_patch_seam_alpha(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const LocalMask& mask,
    int target_x,
    int target_y) {
  const auto feather = std::clamp(mask.inpaint_seam_feather, 0.0, 24.0);
  if (feather <= 1e-6) {
    return 1.0;
  }

  const auto radius = std::max(1, static_cast<int>(std::ceil(feather)));
  auto nearest_outside = feather + 1.0;
  auto found_outside = false;
  for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
    const auto sample_y = target_y + offset_y;
    if (sample_y < 0 || sample_y >= height) {
      continue;
    }
    for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
      const auto sample_x = target_x + offset_x;
      if (sample_x < 0 || sample_x >= width) {
        continue;
      }
      const auto distance = std::sqrt(
          static_cast<double>(offset_x * offset_x + offset_y * offset_y));
      if (distance > feather + 0.5) {
        continue;
      }

      const auto sample_index = static_cast<std::size_t>(sample_y * width + sample_x);
      const auto normalized_x =
          width > 1 ? static_cast<double>(sample_x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y =
          height > 1 ? static_cast<double>(sample_y) / static_cast<double>(height - 1) : 0.5;
      const auto matte =
          local_mask_weight(normalized_x, normalized_y, pixels[sample_index], context, mask);
      if (matte <= 0.05) {
        nearest_outside = std::min(nearest_outside, distance);
        found_outside = true;
      }
    }
  }

  if (!found_outside) {
    return 1.0;
  }
  return smoothstep(0.0, feather, nearest_outside);
}

[[nodiscard]] bool has_photo_layers(const std::vector<PhotoLayer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind != "group" && layer.enabled && layer.opacity > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] double photo_layer_mask_weight(
    const PhotoLayer& layer,
    int x,
    int y,
    int width,
    int height) {
  const auto expected_size =
      static_cast<std::size_t>(layer.mask_width) * static_cast<std::size_t>(layer.mask_height);
  if (layer.mask_width <= 0 || layer.mask_height <= 0 || layer.mask_data.size() != expected_size ||
      width <= 0 || height <= 0) {
    return 1.0;
  }

  const auto normalized_x =
      width > 1 ? static_cast<double>(std::clamp(x, 0, width - 1)) / static_cast<double>(width - 1)
                : 0.0;
  const auto normalized_y =
      height > 1 ? static_cast<double>(std::clamp(y, 0, height - 1)) / static_cast<double>(height - 1)
                 : 0.0;
  const auto mask_x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(layer.mask_width - 1))),
      0,
      layer.mask_width - 1);
  const auto mask_y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(layer.mask_height - 1))),
      0,
      layer.mask_height - 1);
  const auto index = static_cast<std::size_t>(mask_y * layer.mask_width + mask_x);
  auto weight = static_cast<double>(layer.mask_data[index]) / 255.0;
  if (layer.mask_inverted) {
    weight = 1.0 - weight;
  }
  weight = std::min(weight, std::clamp(layer.mask_density, 0.0, 1.0));
  return clamp_unit(weight * std::clamp(layer.mask_opacity, 0.0, 1.0));
}

struct HslColor final {
  double hue = 0.0;
  double saturation = 0.0;
  double lightness = 0.0;
};

[[nodiscard]] double hue_to_rgb(double p, double q, double t) {
  if (t < 0.0) {
    t += 1.0;
  }
  if (t > 1.0) {
    t -= 1.0;
  }
  if (t < 1.0 / 6.0) {
    return p + (q - p) * 6.0 * t;
  }
  if (t < 0.5) {
    return q;
  }
  if (t < 2.0 / 3.0) {
    return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  }
  return p;
}

[[nodiscard]] ColorPixel hsl_to_rgb(const HslColor& hsl) {
  if (hsl.saturation <= 1e-9) {
    const auto gray = static_cast<float>(clamp_unit(hsl.lightness));
    return ColorPixel{gray, gray, gray, 1.0F};
  }
  const auto q = hsl.lightness < 0.5
                     ? hsl.lightness * (1.0 + hsl.saturation)
                     : hsl.lightness + hsl.saturation - hsl.lightness * hsl.saturation;
  const auto p = 2.0 * hsl.lightness - q;
  return ColorPixel{
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue + 1.0 / 3.0))),
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue))),
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue - 1.0 / 3.0))),
      1.0F};
}

[[nodiscard]] double oklab_hue_for_rgb_hue(double hue_unit, const PipelineColorContext& context) {
  const auto target_rgb = hsl_to_rgb(HslColor{
      .hue = wrap_unit(hue_unit),
      .saturation = 1.0,
      .lightness = 0.5,
  });
  const auto target_oklab = linear_rgb_to_oklab(target_rgb, context);
  return wrap_radians(std::atan2(target_oklab.b, target_oklab.a));
}

[[nodiscard]] double overlay_channel(double base, double source, double headroom) {
  const auto safe_headroom = std::clamp(headroom, 1.0, 16.0);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  const auto blended = normalized_base <= 0.5
                           ? 2.0 * normalized_base * normalized_source
                           : 1.0 - 2.0 * (1.0 - normalized_base) * (1.0 - normalized_source);
  return blended * safe_headroom;
}

[[nodiscard]] double soft_light_channel(double base, double source, double headroom) {
  const auto safe_headroom = std::clamp(headroom, 1.0, 16.0);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  double blended = normalized_base;
  if (normalized_source <= 0.5) {
    blended = normalized_base -
              (1.0 - 2.0 * normalized_source) * normalized_base * (1.0 - normalized_base);
  } else {
    const auto d = normalized_base <= 0.25
                       ? ((16.0 * normalized_base - 12.0) * normalized_base + 4.0) *
                             normalized_base
                       : std::sqrt(normalized_base);
    blended = normalized_base + (2.0 * normalized_source - 1.0) * (d - normalized_base);
  }
  return blended * safe_headroom;
}

[[nodiscard]] ColorPixel blend_layer_color(
    const ColorPixel& base,
    const ColorPixel& source,
    const std::string& mode,
    const PipelineColorContext& context) {
  const auto headroom = std::clamp(context.output_headroom, 1.0, 16.0);
  const auto base_red = std::max(0.0, static_cast<double>(base.red));
  const auto base_green = std::max(0.0, static_cast<double>(base.green));
  const auto base_blue = std::max(0.0, static_cast<double>(base.blue));
  const auto source_red = std::max(0.0, static_cast<double>(source.red));
  const auto source_green = std::max(0.0, static_cast<double>(source.green));
  const auto source_blue = std::max(0.0, static_cast<double>(source.blue));

  if (mode == "multiply") {
    return ColorPixel{
        static_cast<float>(base_red * source_red),
        static_cast<float>(base_green * source_green),
        static_cast<float>(base_blue * source_blue),
        source.alpha};
  }
  if (mode == "screen") {
    return ColorPixel{
        static_cast<float>(headroom - ((headroom - base_red) * (headroom - source_red) / headroom)),
        static_cast<float>(headroom - ((headroom - base_green) * (headroom - source_green) / headroom)),
        static_cast<float>(headroom - ((headroom - base_blue) * (headroom - source_blue) / headroom)),
        source.alpha};
  }
  if (mode == "overlay") {
    return ColorPixel{
        static_cast<float>(overlay_channel(base_red, source_red, headroom)),
        static_cast<float>(overlay_channel(base_green, source_green, headroom)),
        static_cast<float>(overlay_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "soft light" || mode == "soft-light") {
    return ColorPixel{
        static_cast<float>(soft_light_channel(base_red, source_red, headroom)),
        static_cast<float>(soft_light_channel(base_green, source_green, headroom)),
        static_cast<float>(soft_light_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "hard light" || mode == "hard-light") {
    return ColorPixel{
        static_cast<float>(overlay_channel(source_red, base_red, headroom)),
        static_cast<float>(overlay_channel(source_green, base_green, headroom)),
        static_cast<float>(overlay_channel(source_blue, base_blue, headroom)),
        source.alpha};
  }
  if (mode == "difference") {
    return ColorPixel{
        static_cast<float>(std::abs(base_red - source_red)),
        static_cast<float>(std::abs(base_green - source_green)),
        static_cast<float>(std::abs(base_blue - source_blue)),
        source.alpha};
  }
  if (mode == "lighten") {
    return ColorPixel{
        static_cast<float>(std::max(base_red, source_red)),
        static_cast<float>(std::max(base_green, source_green)),
        static_cast<float>(std::max(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "darken") {
    return ColorPixel{
        static_cast<float>(std::min(base_red, source_red)),
        static_cast<float>(std::min(base_green, source_green)),
        static_cast<float>(std::min(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "linear light" || mode == "linear-light") {
    return ColorPixel{
        static_cast<float>(std::clamp(base_red + 2.0 * source_red - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_green + 2.0 * source_green - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_blue + 2.0 * source_blue - headroom, 0.0, headroom)),
        source.alpha};
  }
  if (mode == "hue" || mode == "saturation" || mode == "color" || mode == "luminosity") {
    const auto base_oklab = linear_rgb_to_oklab(base, context);
    const auto source_oklab = linear_rgb_to_oklab(source, context);
    auto lightness = clamp_oklab_lightness(base_oklab.lightness, context);
    auto chroma = std::hypot(base_oklab.a, base_oklab.b);
    auto hue = wrap_radians(std::atan2(base_oklab.b, base_oklab.a));
    const auto source_chroma = std::hypot(source_oklab.a, source_oklab.b);
    const auto source_hue = source_chroma > 1.0e-6
                                ? wrap_radians(std::atan2(source_oklab.b, source_oklab.a))
                                : hue;
    if (mode == "hue") {
      hue = source_hue;
    } else if (mode == "saturation") {
      chroma = source_chroma;
    } else if (mode == "color") {
      hue = source_hue;
      chroma = source_chroma;
    } else {
      lightness = clamp_oklab_lightness(source_oklab.lightness, context);
    }
    auto blended = gamut_fit_oklch(lightness, chroma, hue, source.alpha, context);
    blended.alpha = source.alpha;
    return blended;
  }
  return source;
}

void composite_layer_pixel(
    ColorPixel* target,
    const ColorPixel& source,
    double alpha,
    const std::string& mode,
    const PipelineColorContext& context) {
  if (target == nullptr || alpha <= 1e-6) {
    return;
  }
  const auto blended = blend_layer_color(*target, source, mode, context);
  target->red = static_cast<float>(
      std::lerp(static_cast<double>(target->red), static_cast<double>(blended.red), alpha));
  target->green = static_cast<float>(
      std::lerp(static_cast<double>(target->green), static_cast<double>(blended.green), alpha));
  target->blue = static_cast<float>(
      std::lerp(static_cast<double>(target->blue), static_cast<double>(blended.blue), alpha));
  target->alpha = static_cast<float>(alpha + static_cast<double>(target->alpha) * (1.0 - alpha));
}

[[nodiscard]] bool photo_layer_uses_normal_blend(const PhotoLayer& layer) {
  return layer.blend_mode.empty() || layer.blend_mode == "normal";
}

[[nodiscard]] bool has_photo_layer_transform(const PhotoLayer& layer) {
  const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
  return std::abs(scale - 1.0) > 1e-6 || std::abs(layer.transform_rotation) > 1e-6;
}

[[nodiscard]] ColorPixel apply_post_tone_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double output_headroom);

[[nodiscard]] bool has_photo_layer_curve_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.curve_shadows) > 1e-9 || std::abs(layer.curve_darks) > 1e-9 ||
         std::abs(layer.curve_lights) > 1e-9 || std::abs(layer.curve_highlights) > 1e-9 ||
         has_point_curve(layer.master_curve_points) || has_point_curve(layer.luma_curve_points) ||
         has_point_curve(layer.red_curve_points) || has_point_curve(layer.green_curve_points) ||
         has_point_curve(layer.blue_curve_points);
}

[[nodiscard]] bool has_photo_layer_advanced_tone_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.brightness) > 1e-9 || std::abs(layer.filmic_shoulder) > 1e-9 ||
         std::abs(layer.filmic_toe) > 1e-9 || std::abs(layer.adaptive_contrast) > 1e-9 ||
         std::abs(layer.levels_black_point) > 1e-9 || std::abs(layer.levels_gamma) > 1e-9 ||
         std::abs(layer.levels_white_point) > 1e-9 ||
         std::abs(layer.zone_deep_shadows) > 1e-9 ||
         std::abs(layer.zone_shadows) > 1e-9 ||
         std::abs(layer.zone_midtones) > 1e-9 ||
         std::abs(layer.zone_lights) > 1e-9 ||
         std::abs(layer.zone_highlights) > 1e-9;
}

[[nodiscard]] double apply_layer_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  return srgb_to_linear(evaluate_curve_data(linear_to_srgb(value), curve));
}

[[nodiscard]] ColorPixel apply_layer_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context) {
  if (curve.points.size() < 2) {
    return pixel;
  }
  const auto source_luminance = luminance_for_pixel(pixel, context);
  const auto target_luminance = apply_layer_perceptual_point_curve(source_luminance, curve);
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}

[[nodiscard]] ColorPixel interpolate_adjustment_pixel(
    const ColorPixel& base,
    const ColorPixel& adjusted,
    double weight) {
  const auto clamped_weight = clamp_unit(weight);
  return ColorPixel{
      .red = static_cast<float>(
          std::lerp(static_cast<double>(base.red), static_cast<double>(adjusted.red), clamped_weight)),
      .green = static_cast<float>(
          std::lerp(static_cast<double>(base.green), static_cast<double>(adjusted.green), clamped_weight)),
      .blue = static_cast<float>(
          std::lerp(static_cast<double>(base.blue), static_cast<double>(adjusted.blue), clamped_weight)),
      .alpha = base.alpha,
  };
}

[[nodiscard]] ColorPixel apply_photo_layer_advanced_tone_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer) {
  ColorAdjustmentState tone_state;
  tone_state.brightness = layer.brightness;
  tone_state.filmic_shoulder = layer.filmic_shoulder;
  tone_state.filmic_toe = layer.filmic_toe;
  tone_state.adaptive_contrast = layer.adaptive_contrast;
  tone_state.levels_black_point = layer.levels_black_point;
  tone_state.levels_gamma = layer.levels_gamma;
  tone_state.levels_white_point = layer.levels_white_point;
  tone_state.zone_deep_shadows = layer.zone_deep_shadows;
  tone_state.zone_shadows = layer.zone_shadows;
  tone_state.zone_midtones = layer.zone_midtones;
  tone_state.zone_lights = layer.zone_lights;
  tone_state.zone_highlights = layer.zone_highlights;

  auto target_luminance = apply_global_tone(luminance_for_pixel(input, context), tone_state);
  constexpr auto kLayerToneOutputHeadroom = 1.0;
  if (layer.filmic_shoulder != 0.0) {
    const auto shoulder_threshold = shoulder_threshold_for_max_luminance(
        target_luminance,
        kLayerToneOutputHeadroom,
        layer.filmic_shoulder);
    if (shoulder_threshold < kLayerToneOutputHeadroom) {
      target_luminance = apply_highlight_shoulder(
          target_luminance,
          shoulder_threshold,
          kLayerToneOutputHeadroom);
    }
  }
  auto pixel = scale_pixel_to_luminance_with_headroom(
      input,
      target_luminance,
      kLayerToneOutputHeadroom,
      context);
  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] ColorPixel apply_photo_layer_curve_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer) {
  ColorAdjustmentState curve_state;
  curve_state.curve_shadows = layer.curve_shadows;
  curve_state.curve_darks = layer.curve_darks;
  curve_state.curve_lights = layer.curve_lights;
  curve_state.curve_highlights = layer.curve_highlights;
  curve_state.master_curve_points = layer.master_curve_points;

  constexpr auto kLayerCurveOutputHeadroom = 1.0;
  auto pixel = input;
  const auto parametric_curve = build_parametric_curve_data(curve_state);
  const auto has_master_point_curve = has_point_curve(layer.master_curve_points);
  const auto curve_target = evaluate_tone_curve_target(
      luminance_for_pixel(pixel, context),
      parametric_curve,
      has_master_point_curve,
      kLayerCurveOutputHeadroom);
  pixel = scale_pixel_to_luminance_with_headroom(
      pixel,
      curve_target,
      kLayerCurveOutputHeadroom,
      context);

  const auto master_curve = build_curve_data(layer.master_curve_points);
  if (master_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, master_curve));
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, master_curve));
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, master_curve));
  }

  const auto luma_curve = build_curve_data(layer.luma_curve_points);
  if (luma_curve.points.size() >= 2) {
    pixel = apply_layer_luminance_point_curve(
        pixel,
        luma_curve,
        kLayerCurveOutputHeadroom,
        context);
  }

  const auto red_curve = build_curve_data(layer.red_curve_points);
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, red_curve));
  }
  const auto green_curve = build_curve_data(layer.green_curve_points);
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, green_curve));
  }
  const auto blue_curve = build_curve_data(layer.blue_curve_points);
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, blue_curve));
  }

  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] ColorAdjustmentState photo_layer_color_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState color_state;
  color_state.color_mixer_model = layer.color_mixer_model;
  color_state.color_mixer_channel = layer.color_mixer_channel;
  color_state.hue_adjustment = layer.hue_adjustment;
  color_state.saturation_adjustment = layer.saturation_adjustment;
  color_state.luminance_adjustment = layer.luminance_adjustment;
  color_state.color_mixer_channels = layer.color_mixer_channels;
  color_state.rgb_channel_mixer = layer.rgb_channel_mixer;
  color_state.black_and_white_enabled = layer.black_and_white_enabled;
  color_state.black_and_white_mixer = layer.black_and_white_mixer;
  color_state.color_replacement_enabled = layer.color_replacement_enabled;
  color_state.color_replacement_source_hue = layer.color_replacement_source_hue;
  color_state.color_replacement_target_hue = layer.color_replacement_target_hue;
  color_state.color_replacement_range = layer.color_replacement_range;
  color_state.color_replacement_softness = layer.color_replacement_softness;
  color_state.color_replacement_strength = layer.color_replacement_strength;
  color_state.color_replacement_preserve_luminance = layer.color_replacement_preserve_luminance;
  color_state.color_replacement_preserve_saturation = layer.color_replacement_preserve_saturation;
  color_state.skin_tone_protection_enabled = layer.skin_tone_protection_enabled;
  color_state.skin_tone_line_hue = layer.skin_tone_line_hue;
  color_state.skin_tone_protection_range = layer.skin_tone_protection_range;
  color_state.skin_tone_protection_strength = layer.skin_tone_protection_strength;
  color_state.color_harmony_enabled = layer.color_harmony_enabled;
  color_state.color_harmony_mode = layer.color_harmony_mode;
  color_state.color_harmony_base_hue = layer.color_harmony_base_hue;
  color_state.color_harmony_strength = layer.color_harmony_strength;
  color_state.lut_enabled = layer.lut_enabled;
  color_state.lut_strength = layer.lut_strength;
  color_state.lut_name = layer.lut_name;
  color_state.lut_table = layer.lut_table;
  color_state.lut_size = layer.lut_size;
  color_state.lut_domain_min = layer.lut_domain_min;
  color_state.lut_domain_max = layer.lut_domain_max;
  color_state.lut_input_color_space = layer.lut_input_color_space;
  color_state.lut_output_color_space = layer.lut_output_color_space;
  color_state.lut_stack = layer.lut_stack;
  color_state.grading_shadows_hue = layer.grading_shadows_hue;
  color_state.grading_shadows_saturation = layer.grading_shadows_saturation;
  color_state.grading_shadows_luminance = layer.grading_shadows_luminance;
  color_state.grading_midtones_hue = layer.grading_midtones_hue;
  color_state.grading_midtones_saturation = layer.grading_midtones_saturation;
  color_state.grading_midtones_luminance = layer.grading_midtones_luminance;
  color_state.grading_highlights_hue = layer.grading_highlights_hue;
  color_state.grading_highlights_saturation = layer.grading_highlights_saturation;
  color_state.grading_highlights_luminance = layer.grading_highlights_luminance;
  color_state.grading_amount = layer.grading_amount;
  color_state.grading_balance = layer.grading_balance;
  return color_state;
}

[[nodiscard]] bool has_photo_layer_color_adjustments(
    const ColorAdjustmentState& color_state,
    const std::vector<UserLutRuntime>& user_luts) {
  return has_rgb_channel_mixer_adjustments(color_state) || color_state.black_and_white_enabled ||
         has_post_tone_color_adjustments(color_state, 0.0, 0.0) || !user_luts.empty();
}

[[nodiscard]] ColorPixel apply_user_luts_to_pixel(
    const ColorPixel& input,
    const std::vector<UserLutRuntime>& user_luts) {
  auto current = input;
  for (const auto& runtime_lut : user_luts) {
    const auto source = current;
    const auto graded = apply_lut(runtime_lut.lut, source);
    current = ColorPixel{
        .red = static_cast<float>(
            static_cast<double>(source.red) * (1.0 - runtime_lut.strength) +
            static_cast<double>(graded.red) * runtime_lut.strength),
        .green = static_cast<float>(
            static_cast<double>(source.green) * (1.0 - runtime_lut.strength) +
            static_cast<double>(graded.green) * runtime_lut.strength),
        .blue = static_cast<float>(
            static_cast<double>(source.blue) * (1.0 - runtime_lut.strength) +
            static_cast<double>(graded.blue) * runtime_lut.strength),
        .alpha = source.alpha,
    };
  }
  return current;
}

[[nodiscard]] ColorPixel apply_photo_layer_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& color_state,
    const std::vector<UserLutRuntime>& user_luts) {
  auto pixel = input;
  if (has_rgb_channel_mixer_adjustments(color_state)) {
    pixel = apply_rgb_channel_mixer(pixel, color_state.rgb_channel_mixer);
  }
  if (color_state.black_and_white_enabled) {
    pixel = apply_black_and_white_mixer(pixel, color_state, context);
  }
  constexpr auto kLayerColorOutputHeadroom = 1.0;
  pixel = apply_post_tone_color_adjustments(
      pixel,
      context,
      color_state,
      0.0,
      0.0,
      0.0,
      kLayerColorOutputHeadroom);
  if (!user_luts.empty()) {
    pixel = apply_user_luts_to_pixel(pixel, user_luts);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] constexpr std::string_view photo_layer_effect_detail_order_contract() {
  return "photo-layer-effect-detail-order:v1|layer-recipe:tone->curves->color->effects-detail|effects-detail:before-mask-opacity-blend|mask-opacity-density:alpha-only|global-spatial-detail:after-layer-composite";
}

[[nodiscard]] bool has_photo_layer_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.dehaze) > 1e-9 || std::abs(layer.texture) > 1e-9 ||
         std::abs(layer.clarity) > 1e-9 || std::abs(layer.sharpening) > 1e-9 ||
         std::abs(layer.luminance_nr) > 1e-9 || std::abs(layer.color_nr) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_spatial_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_optical_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.lens_distortion) > 1e-9 ||
         std::abs(layer.lens_vignette) > 1e-9 ||
         std::abs(layer.chromatic_aberration) > 1e-9 ||
         std::abs(layer.perspective_vertical) > 1e-9 ||
         std::abs(layer.perspective_horizontal) > 1e-9 ||
         std::abs(layer.global_transform_rotation) > 1e-9 ||
         std::abs(layer.global_transform_scale - 1.0) > 1e-9 ||
         std::abs(layer.transform_skew_x) > 1e-9 ||
         std::abs(layer.transform_skew_y) > 1e-9 ||
         std::abs(layer.liquify_strength) > 1e-9;
}

[[nodiscard]] ColorAdjustmentState photo_layer_optical_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState state;
  state.lens_distortion = layer.lens_distortion;
  state.lens_vignette = layer.lens_vignette;
  state.chromatic_aberration = layer.chromatic_aberration;
  state.perspective_vertical = layer.perspective_vertical;
  state.perspective_horizontal = layer.perspective_horizontal;
  state.transform_rotation = layer.global_transform_rotation;
  state.transform_scale = layer.global_transform_scale;
  state.transform_skew_x = layer.transform_skew_x;
  state.transform_skew_y = layer.transform_skew_y;
  state.liquify_center_x = layer.liquify_center_x;
  state.liquify_center_y = layer.liquify_center_y;
  state.liquify_radius = layer.liquify_radius;
  state.liquify_strength = layer.liquify_strength;
  state.content_aware_crop_fill = layer.content_aware_crop_fill;
  return state;
}

struct PhotoLayerEffectDetailContext final {
  const std::vector<float>* luminance = nullptr;
  const std::vector<float>* texture_blur = nullptr;
  const std::vector<float>* clarity_blur = nullptr;
  int width = 0;
  int height = 0;
};

[[nodiscard]] bool valid_photo_layer_effect_detail_context(
    const PhotoLayerEffectDetailContext* detail_context,
    std::size_t index) {
  return detail_context != nullptr &&
         detail_context->luminance != nullptr &&
         detail_context->width > 0 &&
         detail_context->height > 0 &&
         index < detail_context->luminance->size();
}

[[nodiscard]] ColorPixel apply_photo_layer_effect_detail_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const PhotoLayerEffectDetailContext* detail_context = nullptr,
    std::size_t index = 0) {
  auto pixel = input;
  const auto dehaze_strength = clamp_signed_percent(layer.dehaze) / 100.0;
  if (std::abs(dehaze_strength) > 1e-9) {
    auto lab = linear_rgb_to_oklab(pixel, context);
    const auto chroma = std::hypot(lab.a, lab.b);
    if (chroma > 1e-6) {
      const auto scale = std::max(0.0, 1.0 + dehaze_strength * 0.10);
      lab.a *= scale;
      lab.b *= scale;
      pixel = clamp_rgb_to_output_headroom(oklab_to_linear_rgb(lab, pixel.alpha, context), context);
    }
  }

  const auto texture_strength = clamp_signed_percent(layer.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(layer.clarity) / 100.0;
  if ((std::abs(texture_strength) > 1e-9 || std::abs(clarity_strength) > 1e-9) &&
      valid_photo_layer_effect_detail_context(detail_context, index)) {
    const auto& luminance_buffer = *detail_context->luminance;
    const auto safe_width = static_cast<std::size_t>(detail_context->width);
    const auto safe_height = static_cast<std::size_t>(detail_context->height);
    const auto x = index % safe_width;
    const auto y = index / safe_width;
    const auto luminance = static_cast<double>(luminance_buffer[index]);
    auto local_activity = 0.0;
    if (x > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - 1])));
    }
    if (x + 1 < safe_width) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + 1])));
    }
    if (y > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - safe_width])));
    }
    if (y + 1 < safe_height) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + safe_width])));
    }

    const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
    const auto shadow_guard = smoothstep(0.025, 0.12, luminance);
    const auto highlight_headroom = 1.0 - smoothstep(0.74, 0.98, luminance);
    const auto tone_guard = shadow_guard * highlight_headroom;
    auto target_luminance = luminance;

    if (std::abs(texture_strength) > 1e-9 &&
        detail_context->texture_blur != nullptr &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->texture_blur->size() &&
        index < detail_context->clarity_blur->size()) {
      const auto texture_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->texture_blur)[index]);
      const auto broad_edge_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto broad_edge_guard =
          1.0 - smoothstep(0.08, 0.36, std::abs(broad_edge_signal)) * 0.72;
      const auto texture_gain = texture_strength >= 0.0 ? 1.30 : 0.48;
      target_luminance +=
          texture_strength * texture_signal * (0.46 + midtone_weight * 0.70) *
          broad_edge_guard * tone_guard * texture_gain;
    }

    if (std::abs(clarity_strength) > 1e-9 &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->clarity_blur->size()) {
      const auto clarity_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto edge_limit =
          1.0 - smoothstep(0.24, 0.58, std::abs(clarity_signal)) * 0.28;
      const auto structure_gate = smoothstep(0.003, 0.035, local_activity);
      const auto clarity_gain = clarity_strength >= 0.0 ? 1.08 : 0.46;
      target_luminance +=
          clarity_strength * clarity_signal * (0.36 + midtone_weight * 0.82) *
          tone_guard * edge_limit * structure_gate * clarity_gain;
    }

    pixel = scale_pixel_to_luminance_with_headroom(pixel, target_luminance, 1.0, context);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

void apply_photo_layer_sharpen_noise_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const PhotoLayer& layer) {
  if (pixels == nullptr || pixels->empty() || width <= 0 || height <= 0) {
    return;
  }

  const auto sharpen_strength = clamp_signed_percent(layer.sharpening) / 100.0;
  const auto sharpen_radius = std::clamp(layer.sharpen_radius, 0.5, 3.0);
  const auto sharpen_detail = std::clamp(layer.sharpen_detail, 0.0, 100.0) / 100.0;
  const auto sharpen_mask_strength = clamp_signed_percent(layer.sharpen_masking) / 100.0;
  const auto luma_nr_strength = clamp_signed_percent(layer.luminance_nr) / 100.0;
  const auto color_nr_strength = clamp_signed_percent(layer.color_nr) / 100.0;
  if (std::abs(sharpen_strength) <= 1e-6 &&
      std::abs(luma_nr_strength) <= 1e-6 &&
      std::abs(color_nr_strength) <= 1e-6) {
    return;
  }

  const auto pixel_count = pixels->size();
  const auto safe_width = static_cast<std::size_t>(width);
  std::vector<float> lightness_buf(pixel_count);
  std::vector<float> chroma_a_buf(pixel_count);
  std::vector<float> chroma_b_buf(pixel_count);
  BlurWorkspace detail_blur_workspace;

  parallel_for(height, [&](int y) {
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto ok = linear_rgb_to_oklab((*pixels)[index], context);
      lightness_buf[index] = static_cast<float>(ok.lightness);
      chroma_a_buf[index] = static_cast<float>(ok.a);
      chroma_b_buf[index] = static_cast<float>(ok.b);
    }
  });

  if (std::abs(sharpen_strength) > 1e-6) {
    const auto sharp_radius = std::max(1, static_cast<int>(std::lround(sharpen_radius)));
    const auto edge_radius =
        std::max(sharp_radius + 2, static_cast<int>(std::lround(sharpen_radius * 3.0)));
    const auto sharpen_gain = 1.9 + sharpen_detail * 1.15;
    std::vector<float> sharp_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        sharp_radius,
        &sharp_blur,
        &detail_blur_workspace);
    std::vector<float> edge_blur;
    if (sharpen_mask_strength > 1e-6) {
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          edge_radius,
          &edge_blur,
          &detail_blur_workspace);
    }

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto detail_signal = lightness_buf[index] - sharp_blur[index];
        auto mask_gate = 1.0;
        if (sharpen_mask_strength > 1e-6) {
          const auto edge_strength = std::abs(lightness_buf[index] - edge_blur[index]);
          mask_gate = smoothstep(
              0.0015,
              0.012 + 0.06 * sharpen_mask_strength,
              static_cast<double>(edge_strength));
        }
        lightness_buf[index] += static_cast<float>(
            static_cast<double>(detail_signal) * sharpen_strength * sharpen_gain * mask_gate);
      }
    });
  }

  if (std::abs(luma_nr_strength) > 1e-6) {
    std::vector<float> nr_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        2,
        &nr_blur,
        &detail_blur_workspace);
    std::vector<float> nr_edge_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        5,
        &nr_edge_blur,
        &detail_blur_workspace);

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto edge = std::abs(lightness_buf[index] - nr_edge_blur[index]);
        const auto edge_preserve = 1.0 - smoothstep(0.005, 0.04, static_cast<double>(edge));
        const auto blend = luma_nr_strength * 0.85 * edge_preserve;
        lightness_buf[index] = static_cast<float>(
            static_cast<double>(lightness_buf[index]) * (1.0 - blend) +
            static_cast<double>(nr_blur[index]) * blend);
      }
    });
  }

  if (std::abs(color_nr_strength) > 1e-6) {
    std::vector<float> a_blur;
    gaussian_approximate_blur_into(
        chroma_a_buf,
        width,
        height,
        3,
        &a_blur,
        &detail_blur_workspace);
    std::vector<float> b_blur;
    gaussian_approximate_blur_into(
        chroma_b_buf,
        width,
        height,
        3,
        &b_blur,
        &detail_blur_workspace);

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      const auto blend = color_nr_strength * 0.90;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        chroma_a_buf[index] = static_cast<float>(
            static_cast<double>(chroma_a_buf[index]) * (1.0 - blend) +
            static_cast<double>(a_blur[index]) * blend);
        chroma_b_buf[index] = static_cast<float>(
            static_cast<double>(chroma_b_buf[index]) * (1.0 - blend) +
            static_cast<double>(b_blur[index]) * blend);
      }
    });
  }

  parallel_for(height, [&](int y) {
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto lightness =
          clamp_oklab_lightness(static_cast<double>(lightness_buf[index]), context);
      const auto a = static_cast<double>(chroma_a_buf[index]);
      const auto b = static_cast<double>(chroma_b_buf[index]);
      (*pixels)[index] = gamut_fit_oklch(
          lightness,
          std::hypot(a, b),
          std::atan2(b, a),
          (*pixels)[index].alpha,
          context);
    }
  });
}

void apply_photo_layers(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<PhotoLayer>& layers) {
  (void)photo_layer_effect_detail_order_contract();
  if (pixels == nullptr || width <= 0 || height <= 0 || !has_photo_layers(layers)) {
    return;
  }

  std::vector<double> previous_layer_alpha(static_cast<std::size_t>(width * height), 1.0);
  for (const auto& layer : layers) {
    if (layer.kind == "group") {
      continue;
    }
    if (!layer.enabled || layer.opacity <= 1e-6) {
      continue;
    }
    std::vector<double> current_layer_alpha(static_cast<std::size_t>(width * height), 0.0);
    const auto opacity = clamp_unit(layer.opacity);
    if (layer.kind == "adjustment") {
      const auto has_advanced_tone_adjustments = has_photo_layer_advanced_tone_adjustments(layer);
      const auto has_curve_adjustments = has_photo_layer_curve_adjustments(layer);
      const auto layer_color_state = photo_layer_color_adjustment_state(layer);
      const auto layer_user_luts = user_luts_from_state(layer_color_state);
      const auto has_color_adjustments =
          has_photo_layer_color_adjustments(layer_color_state, layer_user_luts);
      const auto has_effect_detail_adjustments = has_photo_layer_effect_detail_adjustments(layer);
      const auto has_spatial_effect_detail_adjustments =
          has_photo_layer_spatial_effect_detail_adjustments(layer);
      const auto has_optical_adjustments = has_photo_layer_optical_adjustments(layer);
      const auto precompute_adjusted_layer =
          has_optical_adjustments || has_effect_detail_adjustments;
      std::vector<ColorPixel> effect_detail_input_pixels;
      std::vector<float> effect_detail_luminance;
      std::vector<float> effect_detail_texture_blur;
      std::vector<float> effect_detail_clarity_blur;
      PhotoLayerEffectDetailContext effect_detail_context;
      const auto apply_layer_recipe_without_effect_detail =
          [&](const ColorPixel& source_pixel) {
            auto adjusted = source_pixel;
            apply_local_adjustment_mask(
                &adjusted,
                context,
                1.0,
                layer.exposure,
                layer.contrast,
                layer.highlights,
                layer.shadows,
                layer.whites,
                layer.blacks,
                layer.temperature,
                layer.tint,
                layer.vibrance,
                layer.saturation,
                layer.hue_shift,
                layer.saturation_shift,
                layer.colorize_hue,
                layer.colorize_strength);
            if (has_advanced_tone_adjustments) {
              adjusted = apply_photo_layer_advanced_tone_adjustments(adjusted, context, layer);
            }
            if (has_curve_adjustments) {
              adjusted = apply_photo_layer_curve_adjustments(adjusted, context, layer);
            }
            if (has_color_adjustments) {
              adjusted = apply_photo_layer_color_adjustments(
                  adjusted,
                  context,
                  layer_color_state,
                  layer_user_luts);
            }
            return adjusted;
          };
      if (precompute_adjusted_layer) {
        effect_detail_input_pixels = *pixels;
        for (auto& source_pixel : effect_detail_input_pixels) {
          source_pixel = apply_layer_recipe_without_effect_detail(source_pixel);
        }
        if (has_optical_adjustments) {
          auto optical_state = photo_layer_optical_adjustment_state(layer);
          apply_optical_corrections(&effect_detail_input_pixels, width, height, optical_state);
        }
        effect_detail_luminance.resize(effect_detail_input_pixels.size());
        for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
          effect_detail_luminance[index] =
              static_cast<float>(luminance_for_pixel(effect_detail_input_pixels[index], context));
        }
        BlurWorkspace effect_detail_blur_workspace;
        if (std::abs(layer.texture) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              2,
              0.00045,
              &effect_detail_texture_blur,
              &effect_detail_blur_workspace);
        }
        if (std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              8,
              0.0018,
              &effect_detail_clarity_blur,
              &effect_detail_blur_workspace);
        }
        effect_detail_context = PhotoLayerEffectDetailContext{
            .luminance = &effect_detail_luminance,
            .texture_blur = effect_detail_texture_blur.empty() ? nullptr : &effect_detail_texture_blur,
            .clarity_blur = effect_detail_clarity_blur.empty() ? nullptr : &effect_detail_clarity_blur,
            .width = width,
            .height = height,
        };
        if (has_effect_detail_adjustments) {
          for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
            effect_detail_input_pixels[index] = apply_photo_layer_effect_detail_adjustments(
                effect_detail_input_pixels[index],
                context,
                layer,
                has_spatial_effect_detail_adjustments ? &effect_detail_context : nullptr,
                index);
          }
          apply_photo_layer_sharpen_noise_adjustments(
              &effect_detail_input_pixels,
              width,
              height,
              context,
              layer);
        }
      }
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          auto& pixel = (*pixels)[index];
          const auto clip_weight = layer.clipping_mask ? previous_layer_alpha[index] : 1.0;
          const auto layer_weight =
              opacity * clip_weight * photo_layer_mask_weight(layer, x, y, width, height);
          current_layer_alpha[index] = layer_weight;
          if (layer_weight <= 1e-6) {
            continue;
          }
          const auto before_layer = pixel;
          auto adjusted = precompute_adjusted_layer
                              ? effect_detail_input_pixels[index]
                              : apply_layer_recipe_without_effect_detail(before_layer);
          if (photo_layer_uses_normal_blend(layer)) {
            pixel = interpolate_adjustment_pixel(before_layer, adjusted, layer_weight);
          } else {
            composite_layer_pixel(&pixel, adjusted, layer_weight, layer.blend_mode, context);
          }
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if (layer.kind == "fill") {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          const auto clip_weight = layer.clipping_mask ? previous_layer_alpha[index] : 1.0;
          const auto alpha = clamp_unit(
              static_cast<double>(layer.fill_color.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, x, y, width, height));
          current_layer_alpha[index] = alpha;
          if (alpha <= 1e-6) {
            continue;
          }
          auto& target = (*pixels)[index];
          composite_layer_pixel(&target, layer.fill_color, alpha, layer.blend_mode, context);
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if ((layer.kind != "pixel" && layer.kind != "text" && layer.kind != "source") ||
        layer.pixel_width <= 0 || layer.pixel_height <= 0 ||
        layer.pixels.size() != static_cast<std::size_t>(layer.pixel_width * layer.pixel_height)) {
      continue;
    }
    if (!has_photo_layer_transform(layer)) {
      for (int layer_y = 0; layer_y < layer.pixel_height; ++layer_y) {
        const auto target_y = layer.y + layer_y;
        if (target_y < 0 || target_y >= height) {
          continue;
        }
        for (int layer_x = 0; layer_x < layer.pixel_width; ++layer_x) {
          const auto target_x = layer.x + layer_x;
          if (target_x < 0 || target_x >= width) {
            continue;
          }
          const auto source_index = static_cast<std::size_t>(layer_y * layer.pixel_width + layer_x);
          const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
          const auto& source = layer.pixels[source_index];
          auto& target = (*pixels)[target_index];
          const auto clip_weight = layer.clipping_mask ? previous_layer_alpha[target_index] : 1.0;
          const auto alpha = clamp_unit(
              static_cast<double>(source.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, target_x, target_y, width, height));
          current_layer_alpha[target_index] = std::max(current_layer_alpha[target_index], alpha);
          if (alpha <= 1e-6) {
            continue;
          }
          composite_layer_pixel(&target, source, alpha, layer.blend_mode, context);
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }

    const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
    const auto radians = -layer.transform_rotation * kPi / 180.0;
    const auto cos_rotation = std::cos(radians);
    const auto sin_rotation = std::sin(radians);
    const auto center_x = static_cast<double>(layer.x) +
                          (static_cast<double>(layer.pixel_width) * scale * 0.5);
    const auto center_y = static_cast<double>(layer.y) +
                          (static_cast<double>(layer.pixel_height) * scale * 0.5);
    for (int target_y = 0; target_y < height; ++target_y) {
      for (int target_x = 0; target_x < width; ++target_x) {
        const auto dx = static_cast<double>(target_x) + 0.5 - center_x;
        const auto dy = static_cast<double>(target_y) + 0.5 - center_y;
        const auto local_x =
            ((dx * cos_rotation - dy * sin_rotation) / scale) +
            (static_cast<double>(layer.pixel_width) * 0.5) - 0.5;
        const auto local_y =
            ((dx * sin_rotation + dy * cos_rotation) / scale) +
            (static_cast<double>(layer.pixel_height) * 0.5) - 0.5;
        if (local_x < -0.5 || local_y < -0.5 ||
            local_x > static_cast<double>(layer.pixel_width) - 0.5 ||
            local_y > static_cast<double>(layer.pixel_height) - 0.5) {
          continue;
        }
        const auto source_x = std::clamp(
            static_cast<int>(std::lround(local_x)),
            0,
            layer.pixel_width - 1);
        const auto source_y = std::clamp(
            static_cast<int>(std::lround(local_y)),
            0,
            layer.pixel_height - 1);
        const auto source_index = static_cast<std::size_t>(source_y * layer.pixel_width + source_x);
        const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
        const auto& source = layer.pixels[source_index];
        auto& target = (*pixels)[target_index];
        const auto clip_weight = layer.clipping_mask ? previous_layer_alpha[target_index] : 1.0;
        const auto alpha = clamp_unit(
            static_cast<double>(source.alpha) * opacity * clip_weight *
            photo_layer_mask_weight(layer, target_x, target_y, width, height));
        current_layer_alpha[target_index] = std::max(current_layer_alpha[target_index], alpha);
        if (alpha <= 1e-6) {
          continue;
        }
        composite_layer_pixel(&target, source, alpha, layer.blend_mode, context);
      }
    }
    previous_layer_alpha = std::move(current_layer_alpha);
  }
}

void apply_local_inpaint_patches(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  for (const auto& mask : masks) {
    if (!inpaint_patch_available(mask)) {
      continue;
    }

    const auto separated_heal =
        mask.retouch_mode == "heal" &&
        (std::abs(clamp_unit(mask.heal_texture_blend) - 1.0) > 1e-6 ||
         std::abs(clamp_unit(mask.heal_color_blend) - 1.0) > 1e-6);
    auto source_low = std::vector<ColorPixel>{};
    auto patch_low = std::vector<ColorPixel>{};
    if (separated_heal) {
      source_low = low_frequency_pixels(*pixels, width, height, 2);
      patch_low = low_frequency_pixels(
          mask.inpaint_patch_pixels,
          mask.inpaint_patch_width,
          mask.inpaint_patch_height,
          2);
      if (source_low.size() != pixels->size() ||
          patch_low.size() != mask.inpaint_patch_pixels.size()) {
        source_low.clear();
        patch_low.clear();
      }
    }
    const auto seam_color_delta =
        inpaint_patch_seam_color_delta(*pixels, width, height, context, mask);

    for (int patch_y = 0; patch_y < mask.inpaint_patch_height; ++patch_y) {
      const auto target_y = mask.inpaint_patch_y + patch_y;
      if (target_y < 0 || target_y >= height) {
        continue;
      }
      for (int patch_x = 0; patch_x < mask.inpaint_patch_width; ++patch_x) {
        const auto target_x = mask.inpaint_patch_x + patch_x;
        if (target_x < 0 || target_x >= width) {
          continue;
        }

        const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
        const auto patch_index =
            static_cast<std::size_t>(patch_y * mask.inpaint_patch_width + patch_x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(target_x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(target_y) / static_cast<double>(height - 1) : 0.5;
        const auto weight =
            local_mask_weight(normalized_x, normalized_y, (*pixels)[target_index], context, mask) *
            clamp_unit(mask.remove_strength / 100.0) *
            clamp_unit(mask.inpaint_blend_amount) *
            inpaint_patch_seam_alpha(*pixels, width, height, context, mask, target_x, target_y);
        if (weight <= 1e-4) {
          continue;
        }

        const auto patch_pixel = matched_inpaint_patch_pixel(
            mask.inpaint_patch_pixels[patch_index],
            seam_color_delta,
            mask.inpaint_color_match);
        auto& target_pixel = (*pixels)[target_index];
        const auto target =
            !source_low.empty() && !patch_low.empty()
                ? separated_heal_pixel(
                      target_pixel,
                      source_low[target_index],
                      patch_pixel,
                      patch_low[patch_index],
                      mask.heal_texture_blend,
                      mask.heal_color_blend)
                : patch_pixel;
        target_pixel.red = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.red), static_cast<double>(target.red), weight));
        target_pixel.green = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.green), static_cast<double>(target.green), weight));
        target_pixel.blue = static_cast<float>(
            std::lerp(static_cast<double>(target_pixel.blue), static_cast<double>(target.blue), weight));
      }
    }
  }
}

[[nodiscard]] double composite_local_mask_weight(
    double normalized_x,
    double normalized_y,
    const ColorPixel& pixel,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks,
    const EdgeAwareMatteContext* source_context = nullptr) {
  auto composite_weight = 0.0;
  auto initialized = false;

  for (const auto& mask : masks) {
    if (!mask.enabled) {
      continue;
    }

    const auto raw_weight =
        nested_local_mask_weight(normalized_x, normalized_y, pixel, context, mask, masks, source_context);
    accumulate_composite_weight(
        raw_weight, mask.operation, mask.blend_mode, &composite_weight, &initialized);
  }

  return initialized ? clamp_unit(composite_weight) : 0.0;
}

[[nodiscard]] std::vector<float> build_composite_local_mask_weights(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    const InputStageParameters& input_stage,
    const PipelineColorContext& context,
    const std::vector<LocalMask>& masks) {
  const auto pixel_count = static_cast<std::size_t>(width * height);
  std::vector<float> weights(pixel_count, 0.0F);
  if (pixels.size() != pixel_count) {
    return {};
  }
  const EdgeAwareMatteContext source_context{
      .pixels = &pixels,
      .width = width,
      .height = height,
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      const auto pixel = apply_input_stage(pixels[index], input_stage, context);

      const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
      const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      weights[index] = static_cast<float>(
          composite_local_mask_weight(normalized_x, normalized_y, pixel, context, masks, &source_context));
    }
  }

  return weights;
}

std::uint64_t refined_local_mask_hash_impl(
    const LocalMask& mask,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  const auto safe_width = std::max(width, 0);
  const auto safe_height = std::max(height, 0);
  auto hash = matte_hash_append_string(kOffset, "photo-editor-refined-local-mask-v1");
  hash = matte_hash_append(hash, static_cast<std::uint64_t>(safe_width));
  hash = matte_hash_append(hash, static_cast<std::uint64_t>(safe_height));
  if (safe_width <= 0 || safe_height <= 0) {
    return hash;
  }

  const auto pixel_count = static_cast<std::size_t>(safe_width) * static_cast<std::size_t>(safe_height);
  std::vector<ColorPixel> source_copy;
  const auto has_source_pixels = source_pixels.size() == pixel_count;
  if (has_source_pixels) {
    source_copy.assign(source_pixels.begin(), source_pixels.end());
  }
  const EdgeAwareMatteContext source_context{
      .pixels = has_source_pixels ? &source_copy : nullptr,
      .width = has_source_pixels ? safe_width : 0,
      .height = has_source_pixels ? safe_height : 0,
  };
  const auto* source_context_ptr = has_source_pixels ? &source_context : nullptr;
  const auto context = make_pipeline_color_context(ColorAdjustmentState{});
  const auto neutral_pixel = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F};
  for (int y = 0; y < safe_height; ++y) {
    for (int x = 0; x < safe_width; ++x) {
      const auto index = static_cast<std::size_t>(y * safe_width + x);
      const auto& pixel = has_source_pixels ? source_copy[index] : neutral_pixel;
      const auto normalized_x = safe_width > 1 ? static_cast<double>(x) / static_cast<double>(safe_width - 1) : 0.5;
      const auto normalized_y = safe_height > 1 ? static_cast<double>(y) / static_cast<double>(safe_height - 1) : 0.5;
      const auto quantized_weight = static_cast<std::uint64_t>(
          std::llround(clamp_unit(local_mask_weight(normalized_x, normalized_y, pixel, context, mask, source_context_ptr)) *
                      65535.0));
      hash = matte_hash_append(hash, quantized_weight);
    }
  }
  return hash;
}

std::uint64_t refined_local_mask_set_hash_impl(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  const auto safe_width = std::max(width, 0);
  const auto safe_height = std::max(height, 0);
  auto hash = matte_hash_append_string(kOffset, "photo-editor-refined-local-mask-set-v1");
  hash = matte_hash_append(hash, static_cast<std::uint64_t>(safe_width));
  hash = matte_hash_append(hash, static_cast<std::uint64_t>(safe_height));
  if (safe_width <= 0 || safe_height <= 0) {
    return hash;
  }

  const auto pixel_count = static_cast<std::size_t>(safe_width) * static_cast<std::size_t>(safe_height);
  std::vector<ColorPixel> source_copy;
  const auto has_source_pixels = source_pixels.size() == pixel_count;
  if (has_source_pixels) {
    source_copy.assign(source_pixels.begin(), source_pixels.end());
  }
  const EdgeAwareMatteContext source_context{
      .pixels = has_source_pixels ? &source_copy : nullptr,
      .width = has_source_pixels ? safe_width : 0,
      .height = has_source_pixels ? safe_height : 0,
  };
  const auto* source_context_ptr = has_source_pixels ? &source_context : nullptr;
  const auto context = make_pipeline_color_context(ColorAdjustmentState{});
  const auto neutral_pixel = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F};
  for (int y = 0; y < safe_height; ++y) {
    for (int x = 0; x < safe_width; ++x) {
      const auto index = static_cast<std::size_t>(y * safe_width + x);
      const auto& pixel = has_source_pixels ? source_copy[index] : neutral_pixel;
      const auto normalized_x = safe_width > 1 ? static_cast<double>(x) / static_cast<double>(safe_width - 1) : 0.5;
      const auto normalized_y = safe_height > 1 ? static_cast<double>(y) / static_cast<double>(safe_height - 1) : 0.5;
      const auto quantized_weight = static_cast<std::uint64_t>(
          std::llround(clamp_unit(local_mask_set_weight(normalized_x, normalized_y, pixel, context, mask_set, source_context_ptr)) *
                      65535.0));
      hash = matte_hash_append(hash, quantized_weight);
    }
  }
  return hash;
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha_impl(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    const ColorPipelineConfig& color_pipeline) {
  const auto safe_width = std::max(width, 0);
  const auto safe_height = std::max(height, 0);
  if (safe_width <= 0 || safe_height <= 0) {
    return {};
  }

  const auto pixel_count =
      static_cast<std::size_t>(safe_width) * static_cast<std::size_t>(safe_height);
  std::vector<ColorPixel> source_copy;
  const auto has_source_pixels = source_pixels.size() == pixel_count;
  if (has_source_pixels) {
    source_copy.assign(source_pixels.begin(), source_pixels.end());
  }
  const EdgeAwareMatteContext source_context{
      .pixels = has_source_pixels ? &source_copy : nullptr,
      .width = has_source_pixels ? safe_width : 0,
      .height = has_source_pixels ? safe_height : 0,
  };
  const auto* source_context_ptr = has_source_pixels ? &source_context : nullptr;
  ColorAdjustmentState context_state;
  context_state.color_pipeline = color_pipeline;
  const auto context = make_pipeline_color_context(context_state);
  const auto neutral_pixel = ColorPixel{0.5F, 0.5F, 0.5F, 1.0F};

  std::vector<std::uint8_t> alpha(pixel_count, 0);
  for (int y = 0; y < safe_height; ++y) {
    for (int x = 0; x < safe_width; ++x) {
      const auto index = static_cast<std::size_t>(y * safe_width + x);
      const auto& pixel = has_source_pixels ? source_copy[index] : neutral_pixel;
      const auto normalized_x =
          safe_width > 1 ? static_cast<double>(x) / static_cast<double>(safe_width - 1) : 0.5;
      const auto normalized_y =
          safe_height > 1 ? static_cast<double>(y) / static_cast<double>(safe_height - 1) : 0.5;
      alpha[index] = static_cast<std::uint8_t>(
          std::llround(clamp_unit(local_mask_set_weight(
                         normalized_x,
                         normalized_y,
                         pixel,
                         context,
                         mask_set,
                         source_context_ptr)) *
                     255.0));
    }
  }
  return alpha;
}

[[nodiscard, maybe_unused]] bool local_adjustments_supported_by_metal_pre_tone(
    const LocalAdjustmentSettings& adjustments) {
  return std::abs(adjustments.contrast) <= 1e-6 &&
         std::abs(adjustments.highlights) <= 1e-6 &&
         std::abs(adjustments.shadows) <= 1e-6 &&
         std::abs(adjustments.whites) <= 1e-6 &&
         std::abs(adjustments.blacks) <= 1e-6 &&
         std::abs(adjustments.temperature) <= 1e-6 &&
         std::abs(adjustments.tint) <= 1e-6 &&
         std::abs(adjustments.vibrance) <= 1e-6 &&
         std::abs(adjustments.saturation) <= 1e-6 &&
         std::abs(adjustments.hue_shift) <= 1e-6 &&
         std::abs(adjustments.saturation_shift) <= 1e-6 &&
         std::abs(adjustments.colorize_strength) <= 1e-6;
}

[[nodiscard, maybe_unused]] bool local_mask_set_supported_by_metal_pre_tone(
    const LocalMaskSet& mask_set) {
  return std::abs(mask_set.contrast) <= 1e-6 &&
         std::abs(mask_set.highlights) <= 1e-6 &&
         std::abs(mask_set.shadows) <= 1e-6 &&
         std::abs(mask_set.whites) <= 1e-6 &&
         std::abs(mask_set.blacks) <= 1e-6 &&
         std::abs(mask_set.temperature) <= 1e-6 &&
         std::abs(mask_set.tint) <= 1e-6 &&
         std::abs(mask_set.vibrance) <= 1e-6 &&
         std::abs(mask_set.saturation) <= 1e-6 &&
         std::abs(mask_set.hue_shift) <= 1e-6 &&
         std::abs(mask_set.saturation_shift) <= 1e-6 &&
         std::abs(mask_set.colorize_strength) <= 1e-6;
}

[[nodiscard]] ColorPixel gamut_fit_oklch(
    double lightness,
    double chroma,
    double hue,
    double alpha,
    const PipelineColorContext& context) {
  struct GamutFitResult final {
    double lightness = 0.0;
    double chroma = 0.0;
    ColorPixel pixel;
  };
  const auto should_clip =
      !context.output_gamut_mapping_enabled ||
      context.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      context.render_intent == RenderIntent::RelativeColorimetric;

  const auto search_chroma_at_lightness =
      [&](double candidate_lightness, double candidate_chroma, int iterations) -> GamutFitResult {
    candidate_lightness = clamp_oklab_lightness(candidate_lightness, context);
    candidate_chroma = std::max(0.0, candidate_chroma);

    OklabColor candidate_lab{
        .lightness = candidate_lightness,
        .a = candidate_chroma * std::cos(hue),
        .b = candidate_chroma * std::sin(hue),
    };
    auto candidate = oklab_to_linear_rgb(candidate_lab, alpha, context);
    if (should_clip) {
      return GamutFitResult{
          .lightness = candidate_lightness,
          .chroma = candidate_chroma,
          .pixel = clamp_rgb_to_output_headroom(candidate, context),
      };
    }
    if (in_output_gamut(candidate, context)) {
      return GamutFitResult{
          .lightness = candidate_lightness,
          .chroma = candidate_chroma,
          .pixel = clamp_rgb_to_output_headroom(candidate, context),
      };
    }

    auto low = 0.0;
    auto high = candidate_chroma;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const auto mid = (low + high) * 0.5;
      candidate_lab = OklabColor{
          .lightness = candidate_lightness,
          .a = mid * std::cos(hue),
          .b = mid * std::sin(hue),
      };
      candidate = oklab_to_linear_rgb(candidate_lab, alpha, context);
      if (in_output_gamut(candidate, context)) {
        low = mid;
      } else {
        high = mid;
      }
    }

    candidate_lab = OklabColor{
        .lightness = candidate_lightness,
        .a = low * std::cos(hue),
        .b = low * std::sin(hue),
    };
    return GamutFitResult{
        .lightness = candidate_lightness,
        .chroma = low,
        .pixel = clamp_rgb_to_output_headroom(oklab_to_linear_rgb(candidate_lab, alpha, context), context),
    };
  };

  lightness = clamp_oklab_lightness(lightness, context);
  chroma = std::max(0.0, chroma);
  hue = wrap_radians(hue);
  if (chroma <= 1e-9) {
    return clamp_rgb_to_output_headroom(oklab_to_linear_rgb(
        OklabColor{
            .lightness = lightness,
            .a = 0.0,
            .b = 0.0,
        },
        alpha,
        context),
        context);
  }

  auto best_fit = search_chroma_at_lightness(lightness, chroma, 16);
  if (best_fit.chroma > std::max(1e-4, chroma * 0.02)) {
    return best_fit.pixel;
  }
  if (context.output_gamut_mapping_policy == GamutMappingPolicy::PreserveSaturation) {
    return best_fit.pixel;
  }

  const auto target_lightness = 0.5;
  for (int iteration = 1; iteration <= 12; ++iteration) {
    const auto t = static_cast<double>(iteration) / 12.0;
    const auto candidate_lightness = std::lerp(lightness, target_lightness, t);
    const auto candidate_fit = search_chroma_at_lightness(candidate_lightness, chroma, 16);
    if (candidate_fit.chroma > best_fit.chroma + 1e-6 ||
        (std::abs(candidate_fit.chroma - best_fit.chroma) <= 1e-6 &&
         std::abs(candidate_fit.lightness - lightness) < std::abs(best_fit.lightness - lightness))) {
      best_fit = candidate_fit;
    }
  }

  return best_fit.pixel;
}

[[nodiscard]] ColorPixel gamut_fit_oklab(
    double lightness,
    double a,
    double b,
    double alpha,
    const PipelineColorContext& context) {
  const auto clamped_lightness = clamp_oklab_lightness(lightness, context);
  const auto chroma_sq = (a * a) + (b * b);
  if (chroma_sq <= 1.0e-18) {
    return gamut_fit_oklch(clamped_lightness, 0.0, 0.0, alpha, context);
  }

  const auto direct = oklab_to_linear_rgb(
      OklabColor{
          .lightness = clamped_lightness,
          .a = a,
          .b = b,
      },
      alpha,
      context);
  if (!context.output_gamut_mapping_enabled ||
      context.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      context.render_intent == RenderIntent::RelativeColorimetric ||
      in_output_gamut(direct, context)) {
    return clamp_rgb_to_output_headroom(direct, context);
  }

  return gamut_fit_oklch(
      clamped_lightness,
      std::sqrt(chroma_sq),
      wrap_radians(std::atan2(b, a)),
      alpha,
      context);
}

}  // namespace

std::uint64_t refined_local_mask_hash(
    const LocalMask& mask,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return refined_local_mask_hash_impl(mask, width, height, source_pixels);
}

std::uint64_t refined_local_mask_set_hash(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return refined_local_mask_set_hash_impl(mask_set, width, height, source_pixels);
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels) {
  return rasterize_local_mask_set_alpha_impl(
      mask_set,
      width,
      height,
      source_pixels,
      default_color_pipeline_config());
}

std::vector<std::uint8_t> rasterize_local_mask_set_alpha(
    const LocalMaskSet& mask_set,
    int width,
    int height,
    std::span<const ColorPixel> source_pixels,
    const ColorPipelineConfig& color_pipeline) {
  return rasterize_local_mask_set_alpha_impl(
      mask_set,
      width,
      height,
      source_pixels,
      color_pipeline);
}

double clamp_signed_percent(double value) { return std::clamp(value, -100.0, 100.0); }

double clamp_unit(double value) { return std::clamp(value, 0.0, 1.0); }

double srgb_to_linear(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.04045) {
    return clamped / 12.92;
  }

  return std::pow((clamped + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.0031308) {
    return clamped * 12.92;
  }

  return 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
}

double perceptual_delta(
    const ColorPixel& left,
    const ColorPixel& right,
    const RgbColorSpace& color_space,
    std::string_view chromatic_adaptation_method) {
  ColorAdjustmentState state;
  state.color_pipeline.working_profile.profile_name = color_space.name;
  state.color_pipeline.working_profile.color_space = color_space;
  state.input_transform.chromatic_adaptation_method = std::string(chromatic_adaptation_method);
  const auto context = make_pipeline_color_context(state);
  const auto left_lab = linear_rgb_to_oklab(left, context);
  const auto right_lab = linear_rgb_to_oklab(right, context);
  const auto delta_l = left_lab.lightness - right_lab.lightness;
  const auto delta_a = left_lab.a - right_lab.a;
  const auto delta_b = left_lab.b - right_lab.b;
  return std::sqrt(delta_l * delta_l + delta_a * delta_a + delta_b * delta_b);
}

double apply_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  return srgb_to_linear(evaluate_curve_data(linear_to_srgb(value), curve));
}

[[nodiscard]] ColorPixel apply_master_rgb_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve) {
  if (curve.points.size() < 2) {
    return pixel;
  }

  // The composite RGB curve is a display-referred curve applied to all three
  // working RGB channels. This intentionally differs from the Luma curve,
  // which remaps Y and preserves chromatic ratios. Keeping that distinction is
  // also required for CPU/export parity with the Metal post-color kernel.
  return ColorPixel{
      .red = static_cast<float>(apply_perceptual_point_curve(pixel.red, curve)),
      .green = static_cast<float>(apply_perceptual_point_curve(pixel.green, curve)),
      .blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, curve)),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] ColorPixel apply_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context) {
  if (curve.points.size() < 2) {
    return pixel;
  }
  const auto source_luminance = luminance_for_pixel(pixel, context);
  const auto target_luminance = apply_perceptual_point_curve(source_luminance, curve);
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}

std::vector<float> build_perceptual_curve_lut(std::span<const CurvePoint> points, int sample_count) {
  if (sample_count <= 0 || !has_point_curve(points)) {
    return {};
  }

  const auto curve = build_curve_data(points);
  std::vector<float> lut(static_cast<std::size_t>(sample_count), 0.0F);
  for (int index = 0; index < sample_count; ++index) {
    const auto input = sample_count > 1
                           ? static_cast<double>(index) / static_cast<double>(sample_count - 1)
                           : 0.0;
    lut[static_cast<std::size_t>(index)] = static_cast<float>(apply_perceptual_point_curve(input, curve));
  }
  return lut;
}

double evaluate_tone_curve(double value, const ColorAdjustmentState& state) {
  if (has_point_curve(state.master_curve_points)) {
    return evaluate_curve_data(value, build_curve_data(state.master_curve_points));
  }

  return evaluate_curve_data(value, build_parametric_curve_data(state));
}

[[nodiscard]] const OutputColorProfile& output_profile_for_target(
    const ColorPipelineConfig& config,
    GamutWarningTarget target) {
  return target == GamutWarningTarget::PreviewDisplay ? config.preview_output_profile
                                                      : config.export_output_profile;
}

[[nodiscard]] Matrix3 working_rgb_to_output_rgb_matrix(
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  ColorAdjustmentState state;
  state.color_pipeline = config;
  state.input_transform.chromatic_adaptation_method = std::string(chromatic_adaptation_method);
  const auto source_context = make_pipeline_color_context(state);
  const auto& target_space = output_profile_for_target(config, target).color_space;
  const auto target_rgb_to_xyz_native = rgb_to_xyz_matrix(target_space);
  const auto target_xyz_native_to_rgb = invert_matrix3(target_rgb_to_xyz_native);
  const auto d65_white = xyz_from_chromaticity(white_point_chromaticity(WhitePoint::D65));
  const auto target_white =
      xyz_from_chromaticity(white_point_chromaticity(target_space.white_point));
  const auto from_d65 = chromatic_adaptation_matrix(
      d65_white,
      target_white,
      chromatic_adaptation_method);
  const auto xyz_d65_to_target_rgb = multiply_matrix3(target_xyz_native_to_rgb, from_d65);
  return multiply_matrix3(xyz_d65_to_target_rgb, source_context.rgb_to_xyz_d65);
}

[[nodiscard]] double output_transform_excursion(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  auto max_excursion = 0.0;
  for (const auto channel : {pixel.red, pixel.green, pixel.blue}) {
    if (channel < 0.0F) {
      max_excursion = std::max(max_excursion, -static_cast<double>(channel));
    } else if (static_cast<double>(channel) > headroom) {
      max_excursion = std::max(max_excursion, static_cast<double>(channel) - headroom);
    }
  }
  return max_excursion;
}

OutputTransformResult apply_output_transform(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  const auto& output_profile = output_profile_for_target(config, target);
  const auto output_matrix =
      working_rgb_to_output_rgb_matrix(config, target, chromatic_adaptation_method);
  auto transformed = apply_rgb_matrix(pixel, output_matrix);
  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto max_excursion = output_transform_excursion(transformed, headroom);
  transformed = apply_black_point_compensation(transformed, config);
  const auto should_clip =
      !config.output_gamut_mapping_enabled ||
      config.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      config.render_intent == RenderIntent::RelativeColorimetric;
  transformed = should_clip ? clip_pixel_to_output_headroom(transformed, headroom)
                            : fit_pixel_to_output_headroom(transformed, headroom);
  transformed.alpha = static_cast<float>(clamp_unit(transformed.alpha));
  return OutputTransformResult{
      .pixel = transformed,
      .source_out_of_gamut = max_excursion > 1.0e-6,
      .max_channel_excursion = max_excursion,
      .output_profile_name = output_profile.profile_name,
  };
}

void apply_output_transform(
    std::span<ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  for (auto& pixel : pixels) {
    pixel = apply_output_transform(pixel, config, target, chromatic_adaptation_method).pixel;
  }
}

GamutWarningAnalysis analyze_gamut_warnings(
    std::span<const ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  GamutWarningAnalysis analysis;
  if (pixels.empty()) {
    return analysis;
  }

  for (const auto& pixel : pixels) {
    if (pixel.alpha <= 1.0e-6F) {
      continue;
    }
    ++analysis.tested_pixels;
    const auto output = apply_output_transform(pixel, config, target, chromatic_adaptation_method);
    if (output.source_out_of_gamut) {
      ++analysis.out_of_gamut_pixels;
      analysis.max_channel_excursion =
          std::max(analysis.max_channel_excursion, output.max_channel_excursion);
    }
  }

  if (analysis.tested_pixels > 0) {
    analysis.out_of_gamut_fraction =
        static_cast<double>(analysis.out_of_gamut_pixels) /
        static_cast<double>(analysis.tested_pixels);
  }
  return analysis;
}

double channel_weight_for_selection(double hue, std::string_view channel) {
  if (hue < 0.0) {
    return 0.0;
  }

  struct Region final {
    std::string_view name;
    double center;
    double inner_width;
    double outer_width;
  };

  static constexpr std::array<Region, 8> kRegions{{
      {"Reds", 0.0, 0.05, 0.10},
      {"Oranges", 0.09, 0.04, 0.09},
      {"Yellows", 0.15, 0.04, 0.08},
      {"Greens", 0.32, 0.10, 0.18},
      {"Aquas", 0.50, 0.06, 0.10},
      {"Blues", 0.64, 0.08, 0.13},
      {"Purples", 0.78, 0.06, 0.10},
      {"Magentas", 0.88, 0.05, 0.10},
  }};

  for (const auto& region : kRegions) {
    if (channel != region.name) {
      continue;
    }

    auto distance = std::abs(hue - region.center);
    distance = std::min(distance, 1.0 - distance);
    if (distance <= region.inner_width) {
      return 1.0;
    }
    if (distance >= region.outer_width) {
      return 0.0;
    }

    return 1.0 - smoothstep(region.inner_width, region.outer_width, distance);
  }

  return 0.0;
}

namespace {

ColorPixel apply_post_tone_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& state,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double output_headroom) {
  const auto has_post_color = has_post_tone_color_adjustments(state, saturation_strength, vibrance_strength);
  const auto channel_spread = std::max({
      std::abs(input.red - input.green),
      std::abs(input.green - input.blue),
      std::abs(input.red - input.blue),
  });
  const auto oklab_before_color = linear_rgb_to_oklab(input, context);
  const auto pre_color_lightness = clamp_oklab_lightness(oklab_before_color.lightness, context);
  const auto pre_color_chroma = std::hypot(oklab_before_color.a, oklab_before_color.b);
  const auto highlight_chroma_scale = highlight_chroma_compression_enabled(state, input, output_headroom)
                                          ? highlight_chroma_compression_scale(
                                                pre_color_lightness,
                                                pre_color_chroma)
                                          : 1.0;
  if (!has_post_color && highlight_chroma_scale >= 0.999999) {
    return input;
  }
  if (channel_spread <= 1e-6 && !has_color_grading_adjustments(state) &&
      highlight_chroma_scale >= 0.999999) {
    return input;
  }

  auto oklab = oklab_before_color;
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));
  if (chroma <= 1e-4) {
    chroma = 0.0;
    hue = 0.0;
  }

  auto explicit_skin_protection = 1.0;
  auto replacement_luminance_reference = -1.0;
  auto replacement_luminance_weight = 0.0;
  if (chroma > 1e-6) {
    const auto mixer_model = normalized_color_mixer_model(state);
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto mixer_hue_for_selection = mixer_selection_hue(input, hue, chroma, mixer_model);
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    const auto mid_lightness_weight = 1.0 - smoothstep(0.18, 0.48, std::abs(lightness - 0.56));
    const auto tonal_protection = tone_chroma_protection(lightness);
    explicit_skin_protection = skin_tone_protection_scale(state, normalized_hue, chroma);
    const auto skin_protection =
        default_skin_chroma_protection(normalized_hue, chroma) *
        explicit_skin_protection;
    chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                       tonal_protection * skin_protection * 0.82);
    chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength_for_saturation) * 0.10);
    chroma *= highlight_chroma_scale;

    if (mixer_hue_for_selection >= 0.0) {
      const auto use_legacy_mixer = !has_per_channel_color_mixer_adjustments(state);
      const auto hue_scale = mixer_model == "HSV" ? 0.16 : 0.22;
      const auto saturation_scale = mixer_model == "LCH" ? 0.72 : (mixer_model == "HSV" ? 0.92 : 1.15);
      const auto luminance_scale = mixer_model == "HSV" ? 0.14 : (mixer_model == "LCH" ? 0.20 : 0.30);
      for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
        auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
        if (use_legacy_mixer &&
            state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
          mixer.hue = state.hue_adjustment;
          mixer.saturation = state.saturation_adjustment;
          mixer.luminance = state.luminance_adjustment;
        }
        const auto mixer_weight = channel_weight_for_selection(
            mixer_hue_for_selection, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
        if (mixer_weight > 0.0) {
          const auto protected_mixer_weight = mixer_weight * explicit_skin_protection;
          hue = wrap_radians(hue + protected_mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                       hue_scale * 2.0 * kPi);
          chroma *= std::max(
              0.0,
              1.0 + protected_mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) *
                        saturation_scale);
          lightness = clamp_unit(
              lightness + protected_mixer_weight * (clamp_signed_percent(mixer.luminance) / 100.0) *
                              luminance_scale);
          apply_selective_color_adjustment(mixer, protected_mixer_weight, &lightness, &chroma, &hue);
        }
      }
    }
    if (has_color_replacement_adjustments(state)) {
      const auto selection_hue = rgb_hue_for_pixel(input);
      const auto range = std::clamp(state.color_replacement_range / 100.0, 0.01, 1.0) * 0.50;
      const auto softness = clamp_unit(state.color_replacement_softness / 100.0);
      const auto replacement_weight =
          (selection_hue >= 0.0
               ? circular_feathered_selection_weight(
                     selection_hue,
                     wrap_unit(state.color_replacement_source_hue),
                     range,
                     softness)
               : 0.0) *
          clamp_unit(state.color_replacement_strength / 100.0) * explicit_skin_protection;
      if (replacement_weight > 1e-6) {
        if (state.color_replacement_preserve_luminance) {
          replacement_luminance_reference =
              luminance_for_pixel(gamut_fit_oklch(lightness, chroma, hue, input.alpha, context), context);
          replacement_luminance_weight = replacement_weight;
        }
        const auto target_hue = oklab_hue_for_rgb_hue(state.color_replacement_target_hue, context);
        hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * replacement_weight);
        if (!state.color_replacement_preserve_saturation) {
          chroma = std::max(chroma, std::lerp(chroma, 0.16, replacement_weight));
        }
      }
    }
    if (has_color_harmony_adjustments(state)) {
      const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
      const auto target_hue = nearest_color_harmony_hue(state, normalized_hue) * 2.0 * kPi;
      const auto harmony_weight = clamp_unit(state.color_harmony_strength / 100.0) * 0.65 * explicit_skin_protection;
      hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * harmony_weight);
    }
  }

  {
    auto grade_a = chroma * std::cos(hue);
    auto grade_b = chroma * std::sin(hue);

    const auto grading_amount = clamp_unit(state.grading_amount / 100.0);
    const auto apply_grade = [&](double grade_hue, double grade_sat, double grade_lum, double weight) {
      if (weight < 1e-9) {
        return;
      }
      const auto sat_norm = clamp_signed_percent(grade_sat) / 100.0;
      const auto hue_rad = wrap_unit(grade_hue) * 2.0 * kPi;
      const auto lum_norm = clamp_signed_percent(grade_lum) / 100.0;
      const auto scaled_weight = weight * grading_amount;
      const auto intensity = std::abs(sat_norm) * 0.18 * scaled_weight * explicit_skin_protection;
      if (intensity > 1e-9) {
        grade_a += intensity * std::cos(hue_rad);
        grade_b += intensity * std::sin(hue_rad);
      }
      lightness = clamp_unit(lightness + lum_norm * 0.20 * scaled_weight * explicit_skin_protection);
    };

    const auto balance_shift = clamp_signed_percent(state.grading_balance) / 100.0 * 0.18;
    const auto shadow_weight = 1.0 - smoothstep(0.15 - balance_shift, 0.45 - balance_shift, lightness);
    const auto highlight_weight = smoothstep(0.55 - balance_shift, 0.85 - balance_shift, lightness);
    const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

    apply_grade(state.grading_shadows_hue, state.grading_shadows_saturation,
                state.grading_shadows_luminance, shadow_weight);
    apply_grade(state.grading_midtones_hue, state.grading_midtones_saturation,
                state.grading_midtones_luminance, std::max(0.0, midtone_weight));
    apply_grade(state.grading_highlights_hue, state.grading_highlights_saturation,
                state.grading_highlights_luminance, highlight_weight);

    chroma = std::hypot(grade_a, grade_b);
    hue = wrap_radians(std::atan2(grade_b, grade_a));
  }

  if (std::abs(saturation_strength) > 1e-9) {
    chroma *= saturation_strength >= 0.0
                  ? std::max(0.0, 1.0 + saturation_strength * 0.85)
                  : std::max(0.0, 1.0 + saturation_strength);
  }

  auto output = gamut_fit_oklch(lightness, chroma, hue, input.alpha, context);
  if (replacement_luminance_reference >= 0.0 && replacement_luminance_weight > 1e-6) {
    const auto current_luminance = luminance_for_pixel(output, context);
    const auto target_luminance = std::lerp(
        current_luminance,
        std::clamp(replacement_luminance_reference, 0.0, output_headroom),
        clamp_unit(replacement_luminance_weight));
    output = scale_pixel_to_luminance(output, target_luminance, context);
  }
  const auto grading_adds_chroma =
      std::abs(state.grading_shadows_saturation) > 1e-9 ||
      std::abs(state.grading_midtones_saturation) > 1e-9 ||
      std::abs(state.grading_highlights_saturation) > 1e-9;
  if (saturation_strength <= -0.999999 && !grading_adds_chroma &&
      !has_color_replacement_adjustments(state)) {
    const auto neutral = static_cast<float>(std::clamp(
        luminance_for_pixel(output, context),
        0.0,
        output_headroom));
    output.red = neutral;
    output.green = neutral;
    output.blue = neutral;
  }
  return output;
}

ColorPixel apply_global_color_pipeline(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const InputStageParameters& input_stage,
    const ColorAdjustmentState& state,
    const CurveEvaluationData& parametric_curve,
    const CurveEvaluationData& master_curve,
    const CurveEvaluationData& luma_curve,
    const CurveEvaluationData& red_curve,
    const CurveEvaluationData& green_curve,
    const CurveEvaluationData& blue_curve,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double shoulder_threshold,
    double output_headroom) {
  auto pixel = apply_input_stage(input, input_stage, context);

  auto target_luminance = apply_global_tone(luminance_for_pixel(pixel, context), state);
  if (shoulder_threshold < output_headroom) {
    target_luminance = apply_highlight_shoulder(
        target_luminance,
        shoulder_threshold,
        output_headroom);
  }
  const auto curve_target = evaluate_tone_curve_target(
      target_luminance,
      parametric_curve,
      has_point_curve(state.master_curve_points),
      output_headroom);
  pixel = scale_pixel_to_luminance_with_headroom(pixel, curve_target, output_headroom, context);

  if (master_curve.points.size() >= 2) {
    pixel = apply_master_rgb_point_curve(pixel, master_curve);
  }
  if (luma_curve.points.size() >= 2) {
    pixel = apply_luminance_point_curve(pixel, luma_curve, output_headroom, context);
  }
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, red_curve));
  }
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, green_curve));
  }
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, blue_curve));
  }
  if (has_rgb_channel_mixer_adjustments(state)) {
    pixel = apply_rgb_channel_mixer(pixel, state.rgb_channel_mixer);
  }
  if (state.black_and_white_enabled) {
    pixel = apply_black_and_white_mixer(pixel, state, context);
  }

  const auto has_post_color = has_post_tone_color_adjustments(state, saturation_strength, vibrance_strength);
  const auto channel_spread = std::max({
      std::abs(pixel.red - pixel.green),
      std::abs(pixel.green - pixel.blue),
      std::abs(pixel.red - pixel.blue),
  });
  const auto oklab_before_color = linear_rgb_to_oklab(pixel, context);
  const auto pre_color_lightness = clamp_oklab_lightness(oklab_before_color.lightness, context);
  const auto pre_color_chroma = std::hypot(oklab_before_color.a, oklab_before_color.b);
  const auto highlight_chroma_scale = highlight_chroma_compression_enabled(state, pixel, output_headroom)
                                          ? highlight_chroma_compression_scale(
                                                pre_color_lightness,
                                                pre_color_chroma)
                                          : 1.0;
  if (!has_post_color && highlight_chroma_scale >= 0.999999) {
    return pixel;
  }
  if (channel_spread <= 1e-6 && !has_color_grading_adjustments(state) &&
      highlight_chroma_scale >= 0.999999) {
    return pixel;
  }

  auto oklab = oklab_before_color;
  auto lightness = clamp_oklab_lightness(oklab.lightness, context);
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));
  if (chroma <= 1e-4) {
    chroma = 0.0;
    hue = 0.0;
  }

  auto explicit_skin_protection = 1.0;
  auto replacement_luminance_reference = -1.0;
  auto replacement_luminance_weight = 0.0;
  if (chroma > 1e-6) {
    const auto mixer_model = normalized_color_mixer_model(state);
    const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
    const auto mixer_hue_for_selection = mixer_selection_hue(pixel, hue, chroma, mixer_model);
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    const auto mid_lightness_weight = 1.0 - smoothstep(0.18, 0.48, std::abs(lightness - 0.56));
    const auto tonal_protection = tone_chroma_protection(lightness);
    explicit_skin_protection = skin_tone_protection_scale(state, normalized_hue, chroma);
    const auto skin_protection =
        default_skin_chroma_protection(normalized_hue, chroma) *
        explicit_skin_protection;
    chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                       tonal_protection * skin_protection * 0.82);
    chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength_for_saturation) * 0.10);
    chroma *= highlight_chroma_scale;

    if (mixer_hue_for_selection >= 0.0) {
      const auto use_legacy_mixer = !has_per_channel_color_mixer_adjustments(state);
      const auto hue_scale = mixer_model == "HSV" ? 0.16 : 0.22;
      const auto saturation_scale = mixer_model == "LCH" ? 0.72 : (mixer_model == "HSV" ? 0.92 : 1.15);
      const auto luminance_scale = mixer_model == "HSV" ? 0.14 : (mixer_model == "LCH" ? 0.20 : 0.30);
      for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
        auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
        if (use_legacy_mixer &&
            state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
          mixer.hue = state.hue_adjustment;
          mixer.saturation = state.saturation_adjustment;
          mixer.luminance = state.luminance_adjustment;
        }
        const auto mixer_weight = channel_weight_for_selection(
            mixer_hue_for_selection, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
        if (mixer_weight > 0.0) {
          const auto protected_mixer_weight = mixer_weight * explicit_skin_protection;
          hue = wrap_radians(hue + protected_mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                       hue_scale * 2.0 * kPi);
          chroma *= std::max(
              0.0,
              1.0 + protected_mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) * saturation_scale);
          lightness = clamp_unit(
              lightness + protected_mixer_weight * (clamp_signed_percent(mixer.luminance) / 100.0) * luminance_scale);
          apply_selective_color_adjustment(mixer, protected_mixer_weight, &lightness, &chroma, &hue);
        }
      }
    }
    if (has_color_replacement_adjustments(state)) {
      const auto selection_hue = rgb_hue_for_pixel(pixel);
      const auto range = std::clamp(state.color_replacement_range / 100.0, 0.01, 1.0) * 0.50;
      const auto softness = clamp_unit(state.color_replacement_softness / 100.0);
      const auto replacement_weight =
          (selection_hue >= 0.0
               ? circular_feathered_selection_weight(
                     selection_hue,
                     wrap_unit(state.color_replacement_source_hue),
                     range,
                     softness)
               : 0.0) *
          clamp_unit(state.color_replacement_strength / 100.0) * explicit_skin_protection;
      if (replacement_weight > 1e-6) {
        if (state.color_replacement_preserve_luminance) {
          replacement_luminance_reference =
              luminance_for_pixel(gamut_fit_oklch(lightness, chroma, hue, pixel.alpha, context), context);
          replacement_luminance_weight = replacement_weight;
        }
        const auto target_hue = oklab_hue_for_rgb_hue(state.color_replacement_target_hue, context);
        hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * replacement_weight);
        if (!state.color_replacement_preserve_saturation) {
          chroma = std::max(chroma, std::lerp(chroma, 0.16, replacement_weight));
        }
      }
    }
    if (has_color_harmony_adjustments(state)) {
      const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
      const auto target_hue = nearest_color_harmony_hue(state, normalized_hue) * 2.0 * kPi;
      const auto harmony_weight = clamp_unit(state.color_harmony_strength / 100.0) * 0.65 * explicit_skin_protection;
      hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * harmony_weight);
    }
  }

  // 3-way color grading: shadows / midtones / highlights.
  // Each range contributes an a,b shift (hue direction × saturation intensity)
  // and a lightness offset, weighted by where this pixel falls tonally.
  {
    // Reconstruct cartesian a,b from current polar (mixer may have changed them)
    {
      auto grade_a = chroma * std::cos(hue);
      auto grade_b = chroma * std::sin(hue);

      const auto grading_amount = clamp_unit(state.grading_amount / 100.0);
      const auto apply_grade = [&](double grade_hue, double grade_sat, double grade_lum, double weight) {
        if (weight < 1e-9) {
          return;
        }
        const auto sat_norm = clamp_signed_percent(grade_sat) / 100.0;
        const auto hue_rad = wrap_unit(grade_hue) * 2.0 * kPi;
        const auto lum_norm = clamp_signed_percent(grade_lum) / 100.0;
        const auto scaled_weight = weight * grading_amount;
        const auto intensity = std::abs(sat_norm) * 0.18 * scaled_weight * explicit_skin_protection;
        if (intensity > 1e-9) {
          grade_a += intensity * std::cos(hue_rad);
          grade_b += intensity * std::sin(hue_rad);
        }
        lightness = clamp_unit(lightness + lum_norm * 0.20 * scaled_weight * explicit_skin_protection);
      };

      const auto balance_shift = clamp_signed_percent(state.grading_balance) / 100.0 * 0.18;
      const auto shadow_weight = 1.0 - smoothstep(0.15 - balance_shift, 0.45 - balance_shift, lightness);
      const auto highlight_weight = smoothstep(0.55 - balance_shift, 0.85 - balance_shift, lightness);
      const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

      apply_grade(state.grading_shadows_hue, state.grading_shadows_saturation,
                  state.grading_shadows_luminance, shadow_weight);
      apply_grade(state.grading_midtones_hue, state.grading_midtones_saturation,
                  state.grading_midtones_luminance, std::max(0.0, midtone_weight));
      apply_grade(state.grading_highlights_hue, state.grading_highlights_saturation,
                  state.grading_highlights_luminance, highlight_weight);

      chroma = std::hypot(grade_a, grade_b);
      hue = wrap_radians(std::atan2(grade_b, grade_a));
    }
  }

  // Vibrance protects source image color before hue-domain tools. Saturation is
  // intentionally final so it controls chroma introduced by color grading too.
  if (std::abs(saturation_strength) > 1e-9) {
    chroma *= saturation_strength >= 0.0
                  ? std::max(0.0, 1.0 + saturation_strength * 0.85)
                  : std::max(0.0, 1.0 + saturation_strength);
  }

  auto output = gamut_fit_oklch(lightness, chroma, hue, pixel.alpha, context);
  if (replacement_luminance_reference >= 0.0 && replacement_luminance_weight > 1e-6) {
    const auto current_luminance = luminance_for_pixel(output, context);
    const auto target_luminance = std::lerp(
        current_luminance,
        std::clamp(replacement_luminance_reference, 0.0, output_headroom),
        clamp_unit(replacement_luminance_weight));
    output = scale_pixel_to_luminance(output, target_luminance, context);
  }
  const auto grading_adds_chroma =
      std::abs(state.grading_shadows_saturation) > 1e-9 ||
      std::abs(state.grading_midtones_saturation) > 1e-9 ||
      std::abs(state.grading_highlights_saturation) > 1e-9;
  if (saturation_strength <= -0.999999 && !grading_adds_chroma &&
      !has_color_replacement_adjustments(state)) {
    const auto neutral = static_cast<float>(std::clamp(
        luminance_for_pixel(output, context),
        0.0,
        output_headroom));
    output.red = neutral;
    output.green = neutral;
    output.blue = neutral;
  }
  return output;
}

}  // namespace

ColorLut3D build_color_lut(const ColorAdjustmentState& state) {
  const auto curve_points_equal = [](std::span<const CurvePoint> left, std::span<const CurvePoint> right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
      if (std::abs(left[i].x - right[i].x) > 1e-9 || std::abs(left[i].y - right[i].y) > 1e-9) {
        return false;
      }
    }
    return true;
  };
  const auto lut_inputs_equal = [&](const ColorAdjustmentState& left, const ColorAdjustmentState& right) {
    const auto equal = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
    if (left.input_transform.input_profile_name != right.input_transform.input_profile_name ||
        !equal(left.input_transform.exposure, right.input_transform.exposure) ||
        !equal(left.input_transform.temperature, right.input_transform.temperature) ||
        !equal(left.input_transform.tint, right.input_transform.tint) ||
        left.input_transform.raw_input_transform_enabled != right.input_transform.raw_input_transform_enabled ||
        left.input_transform.highlight_recovery_enabled != right.input_transform.highlight_recovery_enabled ||
        left.input_transform.highlight_reconstruction_mode !=
            right.input_transform.highlight_reconstruction_mode ||
        left.input_transform.chromatic_adaptation_method !=
            right.input_transform.chromatic_adaptation_method ||
        left.input_transform.gamut_mapping_enabled != right.input_transform.gamut_mapping_enabled ||
        left.input_transform.lens_correction_enabled != right.input_transform.lens_correction_enabled ||
        left.input_transform.raw_white_balance_override_enabled !=
            right.input_transform.raw_white_balance_override_enabled ||
        left.input_transform.camera_matching_look != right.input_transform.camera_matching_look ||
        !equal(left.input_transform.raw_white_balance_temperature,
               right.input_transform.raw_white_balance_temperature) ||
        !equal(left.input_transform.raw_white_balance_tint, right.input_transform.raw_white_balance_tint) ||
        !equal(left.input_transform.raw_black_level, right.input_transform.raw_black_level) ||
        !equal(left.input_transform.raw_white_level, right.input_transform.raw_white_level) ||
        !equal(left.input_transform.calibration_hue, right.input_transform.calibration_hue) ||
        !equal(left.input_transform.calibration_saturation, right.input_transform.calibration_saturation) ||
        !equal(left.input_transform.red_primary_hue, right.input_transform.red_primary_hue) ||
        !equal(left.input_transform.red_primary_saturation, right.input_transform.red_primary_saturation) ||
        !equal(left.input_transform.green_primary_hue, right.input_transform.green_primary_hue) ||
        !equal(left.input_transform.green_primary_saturation, right.input_transform.green_primary_saturation) ||
        !equal(left.input_transform.blue_primary_hue, right.input_transform.blue_primary_hue) ||
        !equal(left.input_transform.blue_primary_saturation, right.input_transform.blue_primary_saturation) ||
        left.color_pipeline.input_profile.profile_name != right.color_pipeline.input_profile.profile_name ||
        left.color_pipeline.input_profile.kind != right.color_pipeline.input_profile.kind ||
        left.color_pipeline.camera_profile.profile_name != right.color_pipeline.camera_profile.profile_name ||
        left.color_pipeline.camera_profile.profile_format != right.color_pipeline.camera_profile.profile_format ||
        left.color_pipeline.camera_profile.dual_illuminant != right.color_pipeline.camera_profile.dual_illuminant ||
        !equal(left.color_pipeline.input_transform_recipe.source_temperature,
               right.color_pipeline.input_transform_recipe.source_temperature) ||
        !equal(left.color_pipeline.input_transform_recipe.source_tint,
               right.color_pipeline.input_transform_recipe.source_tint) ||
        !equal(left.color_pipeline.input_transform_recipe.baseline_exposure,
               right.color_pipeline.input_transform_recipe.baseline_exposure) ||
        !equal(left.color_pipeline.input_transform_recipe.highlight_headroom,
               right.color_pipeline.input_transform_recipe.highlight_headroom) ||
        left.color_pipeline.working_profile.profile_name != right.color_pipeline.working_profile.profile_name ||
        left.color_pipeline.preview_output_profile.profile_name !=
            right.color_pipeline.preview_output_profile.profile_name ||
        left.color_pipeline.export_output_profile.profile_name !=
            right.color_pipeline.export_output_profile.profile_name ||
        left.color_pipeline.render_intent != right.color_pipeline.render_intent ||
        left.color_pipeline.output_gamut_mapping_enabled !=
            right.color_pipeline.output_gamut_mapping_enabled ||
        left.color_pipeline.output_gamut_mapping_policy !=
            right.color_pipeline.output_gamut_mapping_policy ||
        !equal(left.color_pipeline.output_headroom, right.color_pipeline.output_headroom) ||
        !equal(left.exposure, right.exposure) ||
        !equal(left.contrast, right.contrast) ||
        !equal(left.highlights, right.highlights) ||
        !equal(left.shadows, right.shadows) ||
        !equal(left.whites, right.whites) ||
        !equal(left.blacks, right.blacks) ||
        !equal(left.brightness, right.brightness) ||
        !equal(left.hdr_local_tone_mapping, right.hdr_local_tone_mapping) ||
        !equal(left.filmic_shoulder, right.filmic_shoulder) ||
        !equal(left.filmic_toe, right.filmic_toe) ||
        !equal(left.adaptive_contrast, right.adaptive_contrast) ||
        !equal(left.levels_black_point, right.levels_black_point) ||
        !equal(left.levels_gamma, right.levels_gamma) ||
        !equal(left.levels_white_point, right.levels_white_point) ||
        !equal(left.zone_deep_shadows, right.zone_deep_shadows) ||
        !equal(left.zone_shadows, right.zone_shadows) ||
        !equal(left.zone_midtones, right.zone_midtones) ||
        !equal(left.zone_lights, right.zone_lights) ||
        !equal(left.zone_highlights, right.zone_highlights) ||
        !equal(left.temperature, right.temperature) ||
        !equal(left.tint, right.tint) ||
        !equal(left.dehaze, right.dehaze) ||
        !equal(left.vibrance, right.vibrance) ||
        !equal(left.saturation, right.saturation) ||
        !equal(left.curve_shadows, right.curve_shadows) ||
        !equal(left.curve_darks, right.curve_darks) ||
        !equal(left.curve_lights, right.curve_lights) ||
        !equal(left.curve_highlights, right.curve_highlights) ||
        left.color_mixer_model != right.color_mixer_model ||
        left.color_mixer_channel != right.color_mixer_channel ||
        !equal(left.hue_adjustment, right.hue_adjustment) ||
        !equal(left.saturation_adjustment, right.saturation_adjustment) ||
        !equal(left.luminance_adjustment, right.luminance_adjustment) ||
        !equal(left.rgb_channel_mixer.red_from_red, right.rgb_channel_mixer.red_from_red) ||
        !equal(left.rgb_channel_mixer.red_from_green, right.rgb_channel_mixer.red_from_green) ||
        !equal(left.rgb_channel_mixer.red_from_blue, right.rgb_channel_mixer.red_from_blue) ||
        !equal(left.rgb_channel_mixer.green_from_red, right.rgb_channel_mixer.green_from_red) ||
        !equal(left.rgb_channel_mixer.green_from_green, right.rgb_channel_mixer.green_from_green) ||
        !equal(left.rgb_channel_mixer.green_from_blue, right.rgb_channel_mixer.green_from_blue) ||
        !equal(left.rgb_channel_mixer.blue_from_red, right.rgb_channel_mixer.blue_from_red) ||
        !equal(left.rgb_channel_mixer.blue_from_green, right.rgb_channel_mixer.blue_from_green) ||
        !equal(left.rgb_channel_mixer.blue_from_blue, right.rgb_channel_mixer.blue_from_blue) ||
        left.black_and_white_enabled != right.black_and_white_enabled ||
        left.color_replacement_enabled != right.color_replacement_enabled ||
        !equal(left.color_replacement_source_hue, right.color_replacement_source_hue) ||
        !equal(left.color_replacement_target_hue, right.color_replacement_target_hue) ||
        !equal(left.color_replacement_range, right.color_replacement_range) ||
        !equal(left.color_replacement_softness, right.color_replacement_softness) ||
        !equal(left.color_replacement_strength, right.color_replacement_strength) ||
        left.color_replacement_preserve_luminance != right.color_replacement_preserve_luminance ||
        left.color_replacement_preserve_saturation != right.color_replacement_preserve_saturation ||
        left.skin_tone_protection_enabled != right.skin_tone_protection_enabled ||
        !equal(left.skin_tone_line_hue, right.skin_tone_line_hue) ||
        !equal(left.skin_tone_protection_range, right.skin_tone_protection_range) ||
        !equal(left.skin_tone_protection_strength, right.skin_tone_protection_strength) ||
        left.color_harmony_enabled != right.color_harmony_enabled ||
        left.color_harmony_mode != right.color_harmony_mode ||
        !equal(left.color_harmony_base_hue, right.color_harmony_base_hue) ||
        !equal(left.color_harmony_strength, right.color_harmony_strength) ||
        !equal(left.grading_shadows_hue, right.grading_shadows_hue) ||
        !equal(left.grading_shadows_saturation, right.grading_shadows_saturation) ||
        !equal(left.grading_shadows_luminance, right.grading_shadows_luminance) ||
        !equal(left.grading_midtones_hue, right.grading_midtones_hue) ||
        !equal(left.grading_midtones_saturation, right.grading_midtones_saturation) ||
        !equal(left.grading_midtones_luminance, right.grading_midtones_luminance) ||
        !equal(left.grading_highlights_hue, right.grading_highlights_hue) ||
        !equal(left.grading_highlights_saturation, right.grading_highlights_saturation) ||
        !equal(left.grading_highlights_luminance, right.grading_highlights_luminance) ||
        !equal(left.grading_amount, right.grading_amount) ||
        !equal(left.grading_balance, right.grading_balance) ||
        !curve_points_equal(left.master_curve_points, right.master_curve_points) ||
        !curve_points_equal(left.luma_curve_points, right.luma_curve_points) ||
        !curve_points_equal(left.red_curve_points, right.red_curve_points) ||
        !curve_points_equal(left.green_curve_points, right.green_curve_points) ||
        !curve_points_equal(left.blue_curve_points, right.blue_curve_points)) {
      return false;
    }

    for (std::size_t i = 0; i < left.color_mixer_channels.size(); ++i) {
      if (!equal(left.color_mixer_channels[i].hue, right.color_mixer_channels[i].hue) ||
          !equal(left.color_mixer_channels[i].saturation, right.color_mixer_channels[i].saturation) ||
          !equal(left.color_mixer_channels[i].luminance, right.color_mixer_channels[i].luminance) ||
          !equal(left.color_mixer_channels[i].selective_cyan, right.color_mixer_channels[i].selective_cyan) ||
          !equal(left.color_mixer_channels[i].selective_magenta, right.color_mixer_channels[i].selective_magenta) ||
          !equal(left.color_mixer_channels[i].selective_yellow, right.color_mixer_channels[i].selective_yellow) ||
          !equal(left.color_mixer_channels[i].selective_black, right.color_mixer_channels[i].selective_black)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < left.black_and_white_mixer.size(); ++i) {
      if (!equal(left.black_and_white_mixer[i], right.black_and_white_mixer[i])) {
        return false;
      }
    }
    return true;
  };
  struct LutCache final {
    std::mutex mutex;
    bool has_state = false;
    ColorAdjustmentState state;
    ColorLut3D lut;
  };
  static LutCache cache;
  {
    std::lock_guard lock(cache.mutex);
    if (cache.has_state && lut_inputs_equal(cache.state, state)) {
      return cache.lut;
    }
  }

  ColorLut3D lut;
  lut.size = kLutSize;
  const auto total = static_cast<std::size_t>(kLutSize * kLutSize * kLutSize);
  lut.table.resize(total);

  const auto color_context = make_pipeline_color_context(state);
  const auto input_stage = make_input_stage_parameters(state, color_context);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto luma_curve = build_curve_data(state.luma_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;

  // Pre-pass: find max target luminance across all LUT samples for consistent
  // highlight shoulder rolloff (matches the slow-path global approach).
  auto max_target_luminance = 0.0;
  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        auto sample = apply_input_stage(
            ColorPixel{
                .red = static_cast<float>(r),
                .green = static_cast<float>(g),
                .blue = static_cast<float>(b),
                .alpha = 1.0F,
            },
            input_stage,
            color_context);
        const auto lum = luminance_for_pixel(sample, color_context);
        const auto target = apply_global_tone(lum, state);
        max_target_luminance = std::max(max_target_luminance, target);
      }
    }
  }
  const auto output_headroom = sanitized_output_headroom(state);
  const auto shoulder_threshold =
      shoulder_threshold_for_max_luminance(max_target_luminance, output_headroom, state.filmic_shoulder);

  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        const auto index = static_cast<std::size_t>(
            (ri * kLutSize + gi) * kLutSize + bi);
        lut.table[index] = apply_global_color_pipeline(
            ColorPixel{
                .red = static_cast<float>(r),
                .green = static_cast<float>(g),
                .blue = static_cast<float>(b),
                .alpha = 1.0F,
            },
            color_context,
            input_stage, state,
            parametric_curve,
            master_curve, luma_curve, red_curve, green_curve, blue_curve,
            saturation_strength, vibrance_strength, dehaze_strength,
            shoulder_threshold,
            output_headroom);
      }
    }
  }

  lut.shoulder_threshold = shoulder_threshold;
  lut.shoulder_headroom = output_headroom;
  lut.valid = true;
  {
    std::lock_guard lock(cache.mutex);
    cache.state = state;
    cache.lut = lut;
    cache.has_state = true;
  }
  return lut;
}

namespace {

[[nodiscard]] bool lut_has_expected_table_size(const ColorLut3D& lut) {
  if (lut.size < 2) {
    return false;
  }
  const auto size = static_cast<std::size_t>(lut.size);
  return lut.table.size() == size * size * size;
}

[[nodiscard]] bool lut_has_unit_domain(const ColorLut3D& lut) {
  return std::abs(lut.domain_min[0]) <= 1e-12 &&
         std::abs(lut.domain_min[1]) <= 1e-12 &&
         std::abs(lut.domain_min[2]) <= 1e-12 &&
         std::abs(lut.domain_max[0] - 1.0) <= 1e-12 &&
         std::abs(lut.domain_max[1] - 1.0) <= 1e-12 &&
         std::abs(lut.domain_max[2] - 1.0) <= 1e-12;
}

[[nodiscard]] ColorPixel apply_lut_unchecked(
    const ColorLut3D& lut,
    const ColorPixel& pixel,
    bool unit_domain) {
  const auto size = lut.size;
  const auto size_minus_1 = static_cast<double>(size - 1);
  const auto normalize_channel = [&](double value, int channel) {
    if (unit_domain) {
      return clamp_unit(value);
    }
    const auto min_value = lut.domain_min[static_cast<std::size_t>(channel)];
    const auto max_value = lut.domain_max[static_cast<std::size_t>(channel)];
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value <= min_value) {
      return clamp_unit(value);
    }
    return clamp_unit((value - min_value) / (max_value - min_value));
  };

  const auto rf = normalize_channel(pixel.red, 0) * size_minus_1;
  const auto gf = normalize_channel(pixel.green, 1) * size_minus_1;
  const auto bf = normalize_channel(pixel.blue, 2) * size_minus_1;
  const auto ri = std::min(static_cast<int>(rf), size - 2);
  const auto gi = std::min(static_cast<int>(gf), size - 2);
  const auto bi = std::min(static_cast<int>(bf), size - 2);
  const auto fr = rf - static_cast<double>(ri);
  const auto fg = gf - static_cast<double>(gi);
  const auto fb = bf - static_cast<double>(bi);

  const auto size_t_size = static_cast<std::size_t>(size);
  const auto row_stride = size_t_size * size_t_size;
  const auto plane_offset = static_cast<std::size_t>(ri) * row_stride +
                            static_cast<std::size_t>(gi) * size_t_size +
                            static_cast<std::size_t>(bi);
  const auto next_r = row_stride;
  const auto next_g = size_t_size;
  const auto next_b = std::size_t{1};
  const auto* table = lut.table.data() + plane_offset;

  const auto& c000 = table[0];
  const auto& c001 = table[next_b];
  const auto& c010 = table[next_g];
  const auto& c011 = table[next_g + next_b];
  const auto& c100 = table[next_r];
  const auto& c101 = table[next_r + next_b];
  const auto& c110 = table[next_r + next_g];
  const auto& c111 = table[next_r + next_g + next_b];

  auto lerp3 = [&](auto ColorPixel::*channel) {
    const auto c00 = c000.*channel * (1.0 - fb) + c001.*channel * fb;
    const auto c01 = c010.*channel * (1.0 - fb) + c011.*channel * fb;
    const auto c10 = c100.*channel * (1.0 - fb) + c101.*channel * fb;
    const auto c11 = c110.*channel * (1.0 - fb) + c111.*channel * fb;
    const auto c0 = c00 * (1.0 - fg) + c01 * fg;
    const auto c1 = c10 * (1.0 - fg) + c11 * fg;
    return c0 * (1.0 - fr) + c1 * fr;
  };

  auto result = ColorPixel{
      .red = static_cast<float>(lerp3(&ColorPixel::red)),
      .green = static_cast<float>(lerp3(&ColorPixel::green)),
      .blue = static_cast<float>(lerp3(&ColorPixel::blue)),
      .alpha = pixel.alpha,
  };

  const auto red_green_diff = std::abs(pixel.red - pixel.green);
  const auto green_blue_diff = std::abs(pixel.green - pixel.blue);
  const auto red_blue_diff = std::abs(pixel.red - pixel.blue);
  const auto max_channel_diff = std::max(red_green_diff, std::max(green_blue_diff, red_blue_diff));
  if (max_channel_diff < 1e-9) {
    const auto avg = (result.red + result.green + result.blue) / 3.0;
    result.red = static_cast<float>(avg);
    result.green = static_cast<float>(avg);
    result.blue = static_cast<float>(avg);
  }

  return result;
}

enum class LocalAdjustmentFastPathResult {
  unsupported,
  applied,
  cancelled,
};

[[nodiscard]] bool local_mask_set_is_exposure_only(const LocalMaskSet& mask_set) {
  return std::abs(mask_set.exposure) > 1e-6 &&
         std::abs(mask_set.contrast) <= 1e-6 &&
         std::abs(mask_set.highlights) <= 1e-6 &&
         std::abs(mask_set.shadows) <= 1e-6 &&
         std::abs(mask_set.whites) <= 1e-6 &&
         std::abs(mask_set.blacks) <= 1e-6 &&
         std::abs(mask_set.temperature) <= 1e-6 &&
         std::abs(mask_set.tint) <= 1e-6 &&
         std::abs(mask_set.vibrance) <= 1e-6 &&
         std::abs(mask_set.saturation) <= 1e-6 &&
         std::abs(mask_set.hue_shift) <= 1e-6 &&
         std::abs(mask_set.saturation_shift) <= 1e-6 &&
         std::abs(mask_set.colorize_hue) <= 1e-6 &&
         std::abs(mask_set.colorize_strength) <= 1e-6;
}

[[nodiscard]] bool brush_mask_source_has_default_refinement(const LocalMask& mask) {
  return mask.parent_id.empty() &&
         !mask.invert &&
         !mask.selection_invert &&
         std::abs(mask.opacity - 1.0) <= 1e-6 &&
         std::abs(mask.density - 1.0) <= 1e-6 &&
         std::abs(mask.selection_expand) <= 1e-6 &&
          std::abs(mask.selection_smooth) <= 1e-6 &&
          std::abs(mask.selection_fine_edge) <= 1e-6 &&
          std::abs(mask.selection_edge_contrast) <= 1e-6 &&
          std::abs(mask.selection_decontaminate) <= 1e-6 &&
          std::abs(mask.selection_grow) <= 1e-6 &&
          std::abs(mask.selection_shrink) <= 1e-6 &&
          std::abs(mask.selection_feather) <= 1e-6 &&
          std::abs(mask.selection_blur) <= 1e-6 &&
          std::abs(mask.selection_harden) <= 1e-6 &&
          std::abs(mask.selection_despeckle) <= 1e-6 &&
          std::abs(mask.selection_fill_holes) <= 1e-6 &&
          std::abs(mask.selection_remove_islands) <= 1e-6 &&
          std::abs(mask.refine_luminance_min) <= 1e-6 &&
         std::abs(mask.refine_luminance_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_hue_range - 0.5) <= 1e-6 &&
         std::abs(mask.refine_saturation_min) <= 1e-6 &&
         std::abs(mask.refine_saturation_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_depth_min) <= 1e-6 &&
         std::abs(mask.refine_depth_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_edge_confidence_min) <= 1e-6 &&
          mask.segmentation_mask.empty() &&
          mask.depth_map.empty();
}

[[nodiscard]] LocalAdjustmentCoordinateMapping default_local_adjustment_coordinate_mapping(
    int width,
    int height) {
  return LocalAdjustmentCoordinateMapping{
      .source_x = 0,
      .source_y = 0,
      .source_width = width,
      .source_height = height,
  };
}

[[nodiscard]] bool local_adjustment_coordinate_mapping_is_valid(
    const LocalAdjustmentCoordinateMapping& mapping,
    int width,
    int height) {
  if (mapping.source_x < 0 || mapping.source_y < 0 ||
      mapping.source_width <= 0 || mapping.source_height <= 0) {
    return false;
  }
  if (width <= 0 || height <= 0 ||
      width > mapping.source_width || height > mapping.source_height) {
    return false;
  }
  return mapping.source_x <= mapping.source_width - width &&
         mapping.source_y <= mapping.source_height - height;
}

[[nodiscard]] double normalized_source_coordinate(
    int offset,
    int source_origin,
    int source_dimension) {
  return source_dimension > 1
             ? static_cast<double>(source_origin + offset) /
                   static_cast<double>(source_dimension - 1)
             : 0.5;
}

[[nodiscard]] LocalAdjustmentFastPathResult apply_single_brush_exposure_fast_path(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& coordinate_mapping,
    const PipelineColorContext& color_context,
    CancellationToken cancellation) {
  if (state.local_mask_sets.size() != 1) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto& mask_set = state.local_mask_sets.front();
  if (!mask_set.enabled || !local_mask_set_is_exposure_only(mask_set) ||
      mask_set.sources.size() != 1) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto& source = mask_set.sources.front();
  if (!source.enabled || source.kind != "brush" || source.operation != "add" ||
      (source.blend_mode != "normal" && !source.blend_mode.empty()) ||
      !brush_mask_source_has_default_refinement(source)) {
    return LocalAdjustmentFastPathResult::unsupported;
  }
  const auto expected_mask_size =
      static_cast<std::size_t>(source.segmentation_width) *
      static_cast<std::size_t>(source.segmentation_height);
  if (source.segmentation_mask.empty() || source.segmentation_width <= 0 ||
      source.segmentation_height <= 0 ||
      source.segmentation_mask.size() != expected_mask_size) {
    return LocalAdjustmentFastPathResult::unsupported;
  }

  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  const auto output_headroom = sanitized_output_headroom(state);
  const auto post_local_tone_is_identity = post_local_tone_pipeline_is_identity(state);
  std::vector<double> normalized_x_values(safe_width, 0.5);
  for (int x = 0; x < width; ++x) {
    normalized_x_values[static_cast<std::size_t>(x)] =
        normalized_source_coordinate(
            x,
            coordinate_mapping.source_x,
            coordinate_mapping.source_width);
  }

  std::vector<double> target_luminances;
  std::vector<double> row_max_luminances;
  std::vector<double> row_max_channels;
  std::vector<double> row_min_channels;
  if (post_local_tone_is_identity) {
    row_max_channels.assign(safe_height, 0.0);
    row_min_channels.assign(safe_height, 0.0);
  } else {
    target_luminances.assign(safe_width * safe_height, 0.0);
    row_max_luminances.assign(safe_height, 0.0);
  }

  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    auto row_max_luminance = 0.0;
    auto row_max_channel = 0.0;
    auto row_min_channel = 0.0;
    const auto normalized_y =
        normalized_source_coordinate(
            y,
            coordinate_mapping.source_y,
            coordinate_mapping.source_height);
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x = normalized_x_values[static_cast<std::size_t>(x)];
      const auto mask_weight = brush_mask_weight(normalized_x, normalized_y, source);
      if (mask_weight > 0.0) {
        const auto gain = std::pow(2.0, mask_set.exposure * mask_weight);
        pixel.red = static_cast<float>(static_cast<double>(pixel.red) * gain);
        pixel.green = static_cast<float>(static_cast<double>(pixel.green) * gain);
        pixel.blue = static_cast<float>(static_cast<double>(pixel.blue) * gain);
      }

      if (post_local_tone_is_identity) {
        const auto red = static_cast<double>(pixel.red);
        const auto green = static_cast<double>(pixel.green);
        const auto blue = static_cast<double>(pixel.blue);
        const auto pixel_max = std::max(red, std::max(green, blue));
        const auto pixel_min = std::min(red, std::min(green, blue));
        row_max_channel = std::max(row_max_channel, pixel_max);
        row_min_channel = std::min(row_min_channel, pixel_min);
      } else {
        target_luminances[index] = apply_global_tone(luminance_for_pixel(pixel, color_context), state);
        row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
      }
    }
    if (post_local_tone_is_identity) {
      row_max_channels[static_cast<std::size_t>(y)] = row_max_channel;
      row_min_channels[static_cast<std::size_t>(y)] = row_min_channel;
    } else {
      row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
    }
  });
  if (cancellation.cancelled()) {
    return LocalAdjustmentFastPathResult::cancelled;
  }

  if (post_local_tone_is_identity) {
    const auto max_channel =
        *std::max_element(row_max_channels.cbegin(), row_max_channels.cend());
    const auto min_channel =
        *std::min_element(row_min_channels.cbegin(), row_min_channels.cend());
    if (max_channel <= output_headroom + 1e-6 && min_channel >= -1e-6) {
      return LocalAdjustmentFastPathResult::applied;
    }

    target_luminances.assign(safe_width * safe_height, 0.0);
    row_max_luminances.assign(safe_height, 0.0);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      auto row_max_luminance = 0.0;
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        target_luminances[index] = apply_global_tone(
            luminance_for_pixel((*pixels)[index], color_context),
            state);
        row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
      }
      row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
    });
    if (cancellation.cancelled()) {
      return LocalAdjustmentFastPathResult::cancelled;
    }
  }

  const auto max_target_luminance =
      *std::max_element(row_max_luminances.cbegin(), row_max_luminances.cend());
  const auto shoulder_threshold =
      shoulder_threshold_for_max_luminance(max_target_luminance, output_headroom, state.filmic_shoulder);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto has_master_point_curve = has_point_curve(state.master_curve_points);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto target_luminance = target_luminances[index];
      if (shoulder_threshold < output_headroom) {
        target_luminance =
            apply_highlight_shoulder(target_luminance, shoulder_threshold, output_headroom);
      }
      const auto curve_target = evaluate_tone_curve_target(
          target_luminance,
          parametric_curve,
          has_master_point_curve,
          output_headroom);
      (*pixels)[index] = scale_pixel_to_luminance_with_headroom(
          (*pixels)[index],
          curve_target,
          output_headroom,
          color_context);
    }
  });
  return cancellation.cancelled() ? LocalAdjustmentFastPathResult::cancelled
                                  : LocalAdjustmentFastPathResult::applied;
}

}  // namespace

ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel) {
  if (!lut.valid || !lut_has_expected_table_size(lut)) {
    return pixel;
  }
  return apply_lut_unchecked(lut, pixel, lut_has_unit_domain(lut));
}

bool apply_local_adjustments_to_adjusted_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation) {
  return apply_local_adjustments_to_adjusted_pixels(
      pixels,
      width,
      height,
      state,
      default_local_adjustment_coordinate_mapping(width, height),
      cancellation);
}

bool apply_local_adjustments_to_adjusted_pixels(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    const LocalAdjustmentCoordinateMapping& coordinate_mapping,
    CancellationToken cancellation) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  if (safe_height > 0 && safe_width > std::numeric_limits<std::size_t>::max() / safe_height) {
    return false;
  }
  if (pixels->size() != safe_width * safe_height) {
    return false;
  }
  if (!local_adjustment_coordinate_mapping_is_valid(coordinate_mapping, width, height)) {
    return false;
  }
  if (state.local_mask_sets.empty()) {
    return true;
  }
  if (!state.local_masks.empty() || !state.retouch_masks.empty() ||
      has_photo_layers(state.photo_layers) || state.linear_mask_enabled ||
      state.radial_mask_enabled) {
    return false;
  }
  if (cancellation.cancelled()) {
    return false;
  }

  const auto color_context = make_pipeline_color_context(state);
  const auto fast_path_result = apply_single_brush_exposure_fast_path(
      pixels, width, height, state, coordinate_mapping, color_context, cancellation);
  if (fast_path_result == LocalAdjustmentFastPathResult::applied) {
    return true;
  }
  if (fast_path_result == LocalAdjustmentFastPathResult::cancelled) {
    return false;
  }

  struct LocalMaskSetDescriptor final {
    const LocalMaskSet* mask_set = nullptr;
    bool has_adjustments = false;
    bool has_decontaminate = false;
  };
  std::vector<LocalMaskSetDescriptor> mask_sets;
  mask_sets.reserve(state.local_mask_sets.size());
  for (const auto& mask_set : state.local_mask_sets) {
    const auto has_adjustments = local_mask_set_has_adjustments(mask_set);
    const auto has_decontaminate = has_selection_decontaminate(mask_set);
    if (has_adjustments || has_decontaminate) {
      mask_sets.push_back(LocalMaskSetDescriptor{
          .mask_set = &mask_set,
          .has_adjustments = has_adjustments,
          .has_decontaminate = has_decontaminate,
      });
    }
  }
  if (mask_sets.empty()) {
    return true;
  }

  const auto edge_matte_source_pixels = *pixels;
  const EdgeAwareMatteContext edge_matte_source_context{
      .pixels = &edge_matte_source_pixels,
      .width = width,
      .height = height,
  };
  const auto output_headroom = sanitized_output_headroom(state);
  const auto post_local_tone_is_identity = post_local_tone_pipeline_is_identity(state);
  std::vector<double> target_luminances;
  std::vector<double> row_max_luminances;
  std::vector<double> row_max_channels;
  std::vector<double> row_min_channels;
  if (post_local_tone_is_identity) {
    row_max_channels.assign(safe_height, 0.0);
    row_min_channels.assign(safe_height, 0.0);
  } else {
    target_luminances.assign(safe_width * safe_height, 0.0);
    row_max_luminances.assign(safe_height, 0.0);
  }

  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    auto row_max_luminance = 0.0;
    auto row_max_channel = 0.0;
    auto row_min_channel = 0.0;
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      const auto normalized_x = normalized_source_coordinate(
          x,
          coordinate_mapping.source_x,
          coordinate_mapping.source_width);
      const auto normalized_y = normalized_source_coordinate(
          y,
          coordinate_mapping.source_y,
          coordinate_mapping.source_height);
      for (const auto& descriptor : mask_sets) {
        const auto& mask_set = *descriptor.mask_set;
        const auto mask_weight = local_mask_set_weight(
            normalized_x,
            normalized_y,
            pixel,
            color_context,
            mask_set,
            &edge_matte_source_context);
        if (descriptor.has_adjustments) {
          apply_local_adjustment_mask(
              &pixel,
              color_context,
              mask_weight,
              mask_set.exposure,
              mask_set.contrast,
              mask_set.highlights,
              mask_set.shadows,
              mask_set.whites,
              mask_set.blacks,
              mask_set.temperature,
              mask_set.tint,
              mask_set.vibrance,
              mask_set.saturation,
              mask_set.hue_shift,
              mask_set.saturation_shift,
              mask_set.colorize_hue,
              mask_set.colorize_strength);
        }
        if (descriptor.has_decontaminate) {
          apply_selection_decontaminate_stages(
              &pixel,
              normalized_x,
              normalized_y,
              color_context,
              mask_set,
              &edge_matte_source_context);
        }
      }
      if (post_local_tone_is_identity) {
        const auto red = static_cast<double>(pixel.red);
        const auto green = static_cast<double>(pixel.green);
        const auto blue = static_cast<double>(pixel.blue);
        const auto pixel_max = std::max(red, std::max(green, blue));
        const auto pixel_min = std::min(red, std::min(green, blue));
        row_max_channel = std::max(row_max_channel, pixel_max);
        row_min_channel = std::min(row_min_channel, pixel_min);
      } else {
        target_luminances[index] = apply_global_tone(luminance_for_pixel(pixel, color_context), state);
        row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
      }
    }
    if (post_local_tone_is_identity) {
      row_max_channels[static_cast<std::size_t>(y)] = row_max_channel;
      row_min_channels[static_cast<std::size_t>(y)] = row_min_channel;
    } else {
      row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
    }
  });
  if (cancellation.cancelled()) {
    return false;
  }

  if (post_local_tone_is_identity) {
    const auto max_channel =
        *std::max_element(row_max_channels.cbegin(), row_max_channels.cend());
    const auto min_channel =
        *std::min_element(row_min_channels.cbegin(), row_min_channels.cend());
    if (max_channel <= output_headroom + 1e-6 && min_channel >= -1e-6) {
      return true;
    }

    target_luminances.assign(safe_width * safe_height, 0.0);
    row_max_luminances.assign(safe_height, 0.0);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      auto row_max_luminance = 0.0;
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        target_luminances[index] = apply_global_tone(
            luminance_for_pixel((*pixels)[index], color_context),
            state);
        row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
      }
      row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
    });
    if (cancellation.cancelled()) {
      return false;
    }
  }

  const auto max_target_luminance =
      *std::max_element(row_max_luminances.cbegin(), row_max_luminances.cend());
  const auto shoulder_threshold =
      shoulder_threshold_for_max_luminance(max_target_luminance, output_headroom, state.filmic_shoulder);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto has_master_point_curve = has_point_curve(state.master_curve_points);
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto target_luminance = target_luminances[index];
      if (shoulder_threshold < output_headroom) {
        target_luminance =
            apply_highlight_shoulder(target_luminance, shoulder_threshold, output_headroom);
      }
      const auto curve_target = evaluate_tone_curve_target(
          target_luminance,
          parametric_curve,
          has_master_point_curve,
          output_headroom);
      (*pixels)[index] = scale_pixel_to_luminance_with_headroom(
          (*pixels)[index],
          curve_target,
          output_headroom,
          color_context);
    }
  });
  return !cancellation.cancelled();
}

void apply_color_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    RemovalQuality removal_quality,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  if (telemetry != nullptr) {
    *telemetry = {};
  }
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }
  const auto safe_width = static_cast<std::size_t>(width);
  const auto safe_height = static_cast<std::size_t>(height);
  if (safe_height > 0 && safe_width > std::numeric_limits<std::size_t>::max() / safe_height) {
    return;
  }
  if (pixels->size() != safe_width * safe_height) {
    return;
  }
  if (cancellation.cancelled()) {
    return;
  }

  const auto color_context = make_pipeline_color_context(state);
  const auto final_output_headroom = sanitized_output_headroom(state);
  const auto& effective_retouch_masks =
      state.retouch_masks.empty() ? state.local_masks : state.retouch_masks;
  const auto has_local_patch = has_local_inpaint_patch(effective_retouch_masks);
  if (has_local_patch) {
    apply_local_inpaint_patches(pixels, width, height, color_context, effective_retouch_masks);
    if (cancellation.cancelled()) {
      return;
    }
  }

  apply_optical_corrections(pixels, width, height, state);
  if (cancellation.cancelled()) {
    return;
  }

  const auto edge_matte_source_pixels = *pixels;
  const EdgeAwareMatteContext edge_matte_source_context{
      .pixels = &edge_matte_source_pixels,
      .width = width,
      .height = height,
  };
  const auto texture_strength = clamp_signed_percent(state.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(state.clarity) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;
  const auto sharpen_strength = clamp_signed_percent(state.sharpening) / 100.0;
  const auto sharpen_radius = std::clamp(state.sharpen_radius, 0.5, 3.0);
  const auto sharpen_detail = std::clamp(state.sharpen_detail, 0.0, 100.0) / 100.0;
  const auto sharpen_mask_strength = clamp_signed_percent(state.sharpen_masking) / 100.0;
  const auto luma_nr_strength = clamp_signed_percent(state.luminance_nr) / 100.0;
  const auto color_nr_strength = clamp_signed_percent(state.color_nr) / 100.0;
  const auto user_luts = user_luts_from_state(state);
  const auto hdr_local_tone_strength = clamp_unit(state.hdr_local_tone_mapping / 100.0);
  const auto uses_native_local_mask_sets = !state.local_mask_sets.empty();
  const auto has_spatial_tone_color =
      state.linear_mask_enabled || state.radial_mask_enabled ||
      uses_native_local_mask_sets || !state.local_masks.empty() ||
      has_photo_layers(state.photo_layers) ||
      std::abs(texture_strength) > 1e-6 || std::abs(clarity_strength) > 1e-6 ||
      std::abs(dehaze_strength) > 1e-6 || hdr_local_tone_strength > 1e-6;
  const auto has_detail_effects =
      std::abs(sharpen_strength) > 1e-6 ||
      std::abs(luma_nr_strength) > 1e-6 || std::abs(color_nr_strength) > 1e-6;
  const auto has_user_lut = !user_luts.empty();
  const auto has_local_heal = has_local_mask_heal(effective_retouch_masks);
  const auto has_local_clone = has_local_mask_clone(effective_retouch_masks);
  const auto has_local_red_eye = has_local_mask_red_eye(effective_retouch_masks);
  const auto has_local_skin_cleanup = has_local_mask_skin_cleanup(effective_retouch_masks);
  const auto has_local_frequency_smoothing = has_local_mask_frequency_smoothing(effective_retouch_masks);
  const auto has_local_removal = has_local_mask_removal(effective_retouch_masks);

  // Fast path: no masks or spatial tone/color effects — use precomputed 3D LUT
  if (!has_spatial_tone_color) {
    const auto fast_global_timer = StageClock::now();
    const auto lut = build_color_lut(state);
    if (cancellation.cancelled()) {
      return;
    }
    const auto fast_lut_ready = lut.valid && lut_has_expected_table_size(lut);
    const auto fast_lut_unit_domain = fast_lut_ready && lut_has_unit_domain(lut);

    // Precompute pipeline parameters for exact neutral-pixel computation.
    // Trilinear LUT interpolation uses off-diagonal grid corners that introduce
    // small errors for achromatic inputs; computing directly avoids this.
    const auto fast_input_stage = make_input_stage_parameters(state, color_context);
    const auto fast_parametric_curve = build_parametric_curve_data(state);
    const auto fast_master_curve = build_curve_data(state.master_curve_points);
    const auto fast_luma_curve = build_curve_data(state.luma_curve_points);
    const auto fast_red_curve = build_curve_data(state.red_curve_points);
    const auto fast_green_curve = build_curve_data(state.green_curve_points);
    const auto fast_blue_curve = build_curve_data(state.blue_curve_points);
    const auto fast_sat = clamp_signed_percent(state.saturation) / 100.0;
    const auto fast_vib = clamp_signed_percent(state.vibrance) / 100.0;
    const auto fast_dehaze = clamp_signed_percent(state.dehaze) / 100.0;
    const auto fast_requires_exact_color =
        has_post_tone_color_adjustments(state, fast_sat, fast_vib);
    const auto fast_requires_exact_tone =
        std::abs(effective_input_exposure(state)) > 1e-9 ||
        std::abs(state.contrast) > 1e-9 ||
        std::abs(state.highlights) > 1e-9 ||
        std::abs(state.shadows) > 1e-9 ||
        std::abs(state.whites) > 1e-9 ||
        std::abs(state.blacks) > 1e-9 ||
        std::abs(state.brightness) > 1e-9 ||
        std::abs(state.filmic_shoulder) > 1e-9 ||
        std::abs(state.filmic_toe) > 1e-9 ||
        std::abs(state.adaptive_contrast) > 1e-9 ||
        std::abs(state.levels_black_point) > 1e-9 ||
        std::abs(state.levels_gamma) > 1e-9 ||
        std::abs(state.levels_white_point) > 1e-9 ||
        std::abs(state.zone_deep_shadows) > 1e-9 ||
        std::abs(state.zone_shadows) > 1e-9 ||
        std::abs(state.zone_midtones) > 1e-9 ||
        std::abs(state.zone_lights) > 1e-9 ||
        std::abs(state.zone_highlights) > 1e-9 ||
        std::abs(state.curve_shadows) > 1e-9 ||
        std::abs(state.curve_darks) > 1e-9 ||
        std::abs(state.curve_lights) > 1e-9 ||
        std::abs(state.curve_highlights) > 1e-9 ||
        !state.master_curve_points.empty() ||
        !state.luma_curve_points.empty();
    std::vector<double> fast_row_max_luminances(safe_height, 0.0);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      auto row_max = 0.0;
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto& pixel = (*pixels)[row_offset + static_cast<std::size_t>(x)];
        const auto sample = apply_input_stage(pixel, fast_input_stage, color_context);
        row_max = std::max(
            row_max,
            apply_global_tone(luminance_for_pixel(sample, color_context), state));
      }
      fast_row_max_luminances[static_cast<std::size_t>(y)] = row_max;
    });
    if (cancellation.cancelled()) {
      return;
    }
    const auto fast_max_target_luminance =
        *std::max_element(fast_row_max_luminances.cbegin(), fast_row_max_luminances.cend());
    // Use an image-aware shoulder so true extended-range pixels participate in
    // HDR/SDR tone mapping instead of relying only on the 0-1 LUT sample cube.
    const auto fast_headroom = lut.shoulder_headroom;
    const auto fast_shoulder =
        shoulder_threshold_for_max_luminance(fast_max_target_luminance, fast_headroom, state.filmic_shoulder);

    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        auto& pixel = (*pixels)[row_offset + static_cast<std::size_t>(x)];
        const auto max_diff = std::max({std::abs(pixel.red - pixel.green),
                                         std::abs(pixel.green - pixel.blue),
                                         std::abs(pixel.red - pixel.blue)});
        if (fast_requires_exact_tone || fast_requires_exact_color ||
            max_diff < 1e-9 || has_extended_range_rgb(pixel)) {
          pixel = apply_global_color_pipeline(
              pixel, color_context, fast_input_stage, state,
              fast_parametric_curve,
              fast_master_curve, fast_luma_curve, fast_red_curve, fast_green_curve, fast_blue_curve,
              fast_sat, fast_vib, fast_dehaze, fast_shoulder, fast_headroom);
        } else if (fast_lut_ready) {
          pixel = apply_lut_unchecked(lut, pixel, fast_lut_unit_domain);
        } else {
          pixel = apply_lut(lut, pixel);
        }
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->fast_global_ms += elapsed_stage_ms(fast_global_timer);
    }
  } else {
  if (cancellation.cancelled()) {
    return;
  }

  // Slow path: masks and/or texture/clarity/dehaze require per-pixel spatial processing
  const auto tone_mask_timer = StageClock::now();
  const auto input_stage = make_input_stage_parameters(state, color_context);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto luma_curve = build_curve_data(state.luma_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto uses_legacy_local_mask_stack = !state.local_masks.empty();
  const auto stack_is_additive = local_masks_all_additive(state.local_masks);
  const auto stack_has_selection_decontaminate =
      uses_legacy_local_mask_stack && has_selection_decontaminate(state.local_masks);
  LocalAdjustmentSettings shared_stack_adjustments;
  const auto stack_shares_adjustments =
      uses_legacy_local_mask_stack &&
      local_masks_share_adjustments(state.local_masks, &shared_stack_adjustments);
  if (uses_legacy_local_mask_stack && !stack_shares_adjustments) {
    (void)primary_local_mask_adjustments(state.local_masks, &shared_stack_adjustments);
  }
  const auto uses_composite_local_mask_path =
      uses_legacy_local_mask_stack && (stack_shares_adjustments || !stack_is_additive);
  const auto gradient_radians = state.linear_mask_rotation * kPi / 180.0;
  const auto gradient_cosine = std::cos(gradient_radians);
  const auto gradient_sine = std::sin(gradient_radians);
  const auto gradient_feather = std::max(0.05, state.linear_mask_feather);
  std::vector<float> composite_local_mask_weights;
  if (uses_composite_local_mask_path) {
    composite_local_mask_weights = build_composite_local_mask_weights(
        *pixels,
        width,
        height,
        input_stage,
        color_context,
        state.local_masks);
  }
  std::vector<float> native_local_mask_set_weights;
  const LocalMaskSet* native_local_mask_set_for_metal = nullptr;
  if (uses_native_local_mask_sets &&
      state.local_mask_sets.size() == 1 &&
      local_mask_set_has_adjustments(state.local_mask_sets.front()) &&
      !has_selection_decontaminate(state.local_mask_sets.front()) &&
      local_mask_set_supported_by_metal_pre_tone(state.local_mask_sets.front())) {
    const auto& mask_set = state.local_mask_sets.front();
    native_local_mask_set_weights.resize(pixels->size(), 0.0F);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      const auto normalized_y =
          height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto weight_pixel = apply_input_stage((*pixels)[index], input_stage, color_context);
        native_local_mask_set_weights[index] = static_cast<float>(clamp_unit(
            local_mask_set_weight(
                normalized_x,
                normalized_y,
                weight_pixel,
                color_context,
                mask_set,
                &edge_matte_source_context)));
      }
    });
    if (!cancellation.cancelled()) {
      native_local_mask_set_for_metal = &mask_set;
    }
  }
  const auto uses_simple_global_tone_path =
      !uses_native_local_mask_sets &&
      !uses_legacy_local_mask_stack &&
      !state.linear_mask_enabled &&
      !state.radial_mask_enabled &&
      hdr_local_tone_strength <= 1e-6;
  auto ran_pre_tone_on_gpu = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  {
    auto& metal = metal_backend();
    const auto can_run_local_mask_stack_on_gpu =
        !uses_native_local_mask_sets &&
        uses_composite_local_mask_path &&
        composite_local_mask_weights.size() == pixels->size() &&
        !stack_has_selection_decontaminate &&
        local_adjustments_supported_by_metal_pre_tone(shared_stack_adjustments);
    const auto can_run_native_local_mask_set_on_gpu =
        native_local_mask_set_for_metal != nullptr &&
        native_local_mask_set_weights.size() == pixels->size();
    const auto* metal_local_mask_weights =
        can_run_native_local_mask_set_on_gpu
            ? &native_local_mask_set_weights
            : (can_run_local_mask_stack_on_gpu ? &composite_local_mask_weights : nullptr);
    const auto can_run_pre_tone_on_gpu =
        (!uses_native_local_mask_sets || can_run_native_local_mask_set_on_gpu) &&
        (!uses_legacy_local_mask_stack || can_run_local_mask_stack_on_gpu) &&
        hdr_local_tone_strength <= 1e-6 &&
        metal_color_pipeline_supported(state);
    ran_pre_tone_on_gpu = can_run_pre_tone_on_gpu &&
                          metal.available() &&
                          metal.apply_pre_tone_effects(
                              pixels,
                              width,
                              height,
                              state,
                              metal_local_mask_weights,
                              can_run_native_local_mask_set_on_gpu
                                  ? native_local_mask_set_for_metal->exposure
                                  : (can_run_local_mask_stack_on_gpu
                                         ? shared_stack_adjustments.exposure
                                         : 0.0),
                              can_run_native_local_mask_set_on_gpu
                                  ? native_local_mask_set_for_metal->temperature
                                  : (can_run_local_mask_stack_on_gpu
                                         ? shared_stack_adjustments.temperature
                                         : 0.0),
                              cancellation);
    if (ran_pre_tone_on_gpu && telemetry != nullptr) {
      telemetry->gpu_command_buffer_count += 1;
      telemetry->gpu_kernel_dispatch_count += 2;
      telemetry->gpu_cpu_to_gpu_bytes +=
          static_cast<std::int64_t>(pixels->size() * sizeof(ColorPixel));
      telemetry->gpu_gpu_to_cpu_bytes +=
          static_cast<std::int64_t>(pixels->size() * sizeof(ColorPixel));
    }
  }
#endif

  if (cancellation.cancelled()) {
    return;
  }
  if (!ran_pre_tone_on_gpu) {
    std::vector<double> target_luminances(static_cast<std::size_t>(width * height), 0.0);
    std::vector<double> row_max_luminances(safe_height, 0.0);

    if (uses_simple_global_tone_path) {
      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        auto row_max_luminance = 0.0;
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          auto& pixel = (*pixels)[index];
          pixel = apply_input_stage(pixel, input_stage, color_context);
          target_luminances[index] = apply_global_tone(luminance_for_pixel(pixel, color_context), state);
          row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
        }
        row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
      });
    } else {
      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        auto row_max_luminance = 0.0;
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          auto& pixel = (*pixels)[index];
          pixel = apply_input_stage(pixel, input_stage, color_context);

          const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
          const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
          if (uses_native_local_mask_sets) {
            for (const auto& mask_set : state.local_mask_sets) {
              const auto mask_set_has_adjustments = local_mask_set_has_adjustments(mask_set);
              const auto mask_set_has_decontaminate = has_selection_decontaminate(mask_set);
              if (!mask_set_has_adjustments && !mask_set_has_decontaminate) {
                continue;
              }
              const auto mask_weight =
                  local_mask_set_weight(
                      normalized_x,
                      normalized_y,
                      pixel,
                      color_context,
                      mask_set,
                      &edge_matte_source_context);
              if (mask_set_has_adjustments) {
                apply_local_adjustment_mask(
                    &pixel,
                    color_context,
                    mask_weight,
                    mask_set.exposure,
                    mask_set.contrast,
                    mask_set.highlights,
                    mask_set.shadows,
                    mask_set.whites,
                    mask_set.blacks,
                    mask_set.temperature,
                    mask_set.tint,
                    mask_set.vibrance,
                    mask_set.saturation,
                    mask_set.hue_shift,
                    mask_set.saturation_shift,
                    mask_set.colorize_hue,
                    mask_set.colorize_strength);
              }
              if (mask_set_has_decontaminate) {
                apply_selection_decontaminate_stages(
                    &pixel,
                    normalized_x,
                    normalized_y,
                    color_context,
                    mask_set,
                    &edge_matte_source_context);
              }
            }
          } else if (uses_legacy_local_mask_stack) {
            if (uses_composite_local_mask_path) {
              const auto composite_weight =
                  composite_local_mask_weights.empty()
                      ? composite_local_mask_weight(
                            normalized_x,
                            normalized_y,
                            pixel,
                            color_context,
                            state.local_masks,
                            &edge_matte_source_context)
                      : static_cast<double>(composite_local_mask_weights[index]);
              apply_local_adjustment_mask(
                  &pixel,
                  color_context,
                  composite_weight,
                  shared_stack_adjustments.exposure,
                  shared_stack_adjustments.contrast,
                  shared_stack_adjustments.highlights,
                  shared_stack_adjustments.shadows,
                  shared_stack_adjustments.whites,
                  shared_stack_adjustments.blacks,
                  shared_stack_adjustments.temperature,
                  shared_stack_adjustments.tint,
                  shared_stack_adjustments.vibrance,
                  shared_stack_adjustments.saturation,
                  shared_stack_adjustments.hue_shift,
                  shared_stack_adjustments.saturation_shift,
                  shared_stack_adjustments.colorize_hue,
                  shared_stack_adjustments.colorize_strength);
              if (stack_has_selection_decontaminate) {
                apply_selection_decontaminate_stages(
                    &pixel,
                    normalized_x,
                    normalized_y,
                    color_context,
                    state.local_masks,
                    &edge_matte_source_context);
              }
            } else {
              for (const auto& mask : state.local_masks) {
                apply_local_adjustment_mask(
                    &pixel,
                    color_context,
                    nested_local_mask_weight(
                        normalized_x,
                        normalized_y,
                        pixel,
                        color_context,
                        mask,
                        state.local_masks,
                        &edge_matte_source_context),
                    mask.exposure,
                    mask.contrast,
                    mask.highlights,
                    mask.shadows,
                    mask.whites,
                    mask.blacks,
                    mask.temperature,
                    mask.tint,
                    mask.vibrance,
                    mask.saturation,
                    mask.hue_shift,
                    mask.saturation_shift,
                    mask.colorize_hue,
                    mask.colorize_strength);
              }
              if (stack_has_selection_decontaminate) {
                apply_selection_decontaminate_stages(
                    &pixel,
                    normalized_x,
                    normalized_y,
                    color_context,
                    state.local_masks,
                    &edge_matte_source_context);
              }
            }
          } else {
            if (state.linear_mask_enabled) {
              const auto axis_coordinate =
                  ((normalized_x - 0.5) * gradient_cosine + (normalized_y - 0.5) * gradient_sine) + 0.5;
              const auto mask_weight =
                  clamp_unit(((state.linear_mask_position - axis_coordinate) / gradient_feather) + 0.5);
              apply_local_adjustment_mask(
                  &pixel,
                  color_context,
                  mask_weight,
                  state.linear_mask_exposure,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  state.linear_mask_temperature,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0);
            }
            apply_local_adjustment_mask(
                &pixel,
                color_context,
                radial_mask_weight(normalized_x, normalized_y, state),
                state.radial_mask_exposure,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                state.radial_mask_temperature,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0,
                0.0);
          }

          target_luminances[index] = apply_global_tone(luminance_for_pixel(pixel, color_context), state);
          row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
        }
        row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
      });
    }
    if (cancellation.cancelled()) {
      return;
    }
    auto max_target_luminance =
        *std::max_element(row_max_luminances.cbegin(), row_max_luminances.cend());

    const auto output_headroom = sanitized_output_headroom(state);
    if (hdr_local_tone_strength > 1e-6 && !target_luminances.empty()) {
      std::vector<float> log_luminances(target_luminances.size(), 0.0F);
      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          log_luminances[index] = static_cast<float>(std::log2(std::max(1.0e-6, target_luminances[index])));
        }
      });
      const auto radius = std::clamp(std::max(width, height) / 24, 1, 48);
      const auto local_log_luminance =
          gaussian_approximate_blur(log_luminances, width, height, radius, cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      max_target_luminance = 0.0;
      const auto compression_start = std::log2(0.72);
      std::fill(row_max_luminances.begin(), row_max_luminances.end(), 0.0);
      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        auto row_max_luminance = 0.0;
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          const auto target_log = static_cast<double>(log_luminances[index]);
          const auto local_log = static_cast<double>(local_log_luminance[index]);
          const auto local_excess = std::max(0.0, local_log - compression_start);
          const auto highlight_weight = smoothstep(compression_start, compression_start + 2.2, local_log);
          const auto detail_log = target_log - local_log;
          const auto mapped_log = target_log -
                                  local_excess * hdr_local_tone_strength * 0.58 +
                                  detail_log * hdr_local_tone_strength * highlight_weight * 0.45;
          target_luminances[index] = std::pow(2.0, mapped_log);
          row_max_luminance = std::max(row_max_luminance, target_luminances[index]);
        }
        row_max_luminances[static_cast<std::size_t>(y)] = row_max_luminance;
      });
      if (cancellation.cancelled()) {
        return;
      }
      max_target_luminance =
          *std::max_element(row_max_luminances.cbegin(), row_max_luminances.cend());
    }
    const auto shoulder_threshold =
        shoulder_threshold_for_max_luminance(max_target_luminance, output_headroom, state.filmic_shoulder);
    const auto has_master_point_curve = has_point_curve(state.master_curve_points);
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        auto target_luminance = target_luminances[index];
        if (shoulder_threshold < output_headroom) {
          target_luminance = apply_highlight_shoulder(
              target_luminance,
              shoulder_threshold,
              output_headroom);
        }
        const auto curve_target = evaluate_tone_curve_target(
            target_luminance,
            parametric_curve,
            has_master_point_curve,
            output_headroom);
        (*pixels)[index] = scale_pixel_to_luminance_with_headroom(
            (*pixels)[index],
            curve_target,
            output_headroom,
            color_context);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
  }
  if (telemetry != nullptr) {
    telemetry->tone_mask_ms += elapsed_stage_ms(tone_mask_timer);
  }

  if (has_photo_layers(state.photo_layers)) {
    apply_photo_layers(pixels, width, height, color_context, state.photo_layers);
    if (cancellation.cancelled()) {
      return;
    }
  }

  const auto has_texture_effect = std::abs(texture_strength) > 1e-6;
  const auto has_clarity_effect = std::abs(clarity_strength) > 1e-6;
  const auto has_dehaze_effect = std::abs(dehaze_strength) > 1e-6;
  if (has_texture_effect || has_clarity_effect || has_dehaze_effect) {
    const auto spatial_timer = StageClock::now();
    if (cancellation.cancelled()) {
      return;
    }
    auto ran_spatial_on_gpu = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    {
      auto& metal = metal_backend();
      ran_spatial_on_gpu =
          removal_quality != RemovalQuality::Final &&
          metal.available() &&
          metal_color_pipeline_supported(state) &&
          metal.apply_spatial_tone_effects(
              pixels,
              width,
              height,
              texture_strength,
              clarity_strength,
              dehaze_strength,
              telemetry,
              cancellation);
      if (ran_spatial_on_gpu && telemetry != nullptr) {
        telemetry->spatial_ms += elapsed_stage_ms(spatial_timer);
      }
    }
#endif
    if (!ran_spatial_on_gpu) {
    std::vector<float> luminance_buffer(static_cast<std::size_t>(width * height), 0.0F);
    const auto spatial_luminance_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        luminance_buffer[index] = static_cast<float>(luminance_for_pixel((*pixels)[index], color_context));
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->spatial_luminance_ms += elapsed_stage_ms(spatial_luminance_timer);
    }

    BlurWorkspace spatial_blur_workspace;
    const auto blur_spatial_luminance_into =
        [&](int radius, double epsilon, std::vector<float>* output) {
          if (removal_quality == RemovalQuality::Final) {
            guided_luminance_blur_into(
                luminance_buffer,
                width,
                height,
                radius,
                epsilon,
                output,
                &spatial_blur_workspace,
                cancellation);
            return;
          }
          resize_blur_buffer(&spatial_blur_workspace.horizontal, luminance_buffer.size());
          resize_blur_buffer(output, luminance_buffer.size());
          box_blur_into(
              luminance_buffer,
              width,
              height,
              radius,
              &spatial_blur_workspace.horizontal,
              output,
              cancellation);
        };
    std::vector<float> texture_blur;
    if (has_texture_effect) {
      const auto spatial_texture_blur_timer = StageClock::now();
      blur_spatial_luminance_into(2, 0.00045, &texture_blur);
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->spatial_texture_blur_ms += elapsed_stage_ms(spatial_texture_blur_timer);
      }
    }

    std::vector<float> clarity_blur;
    if (has_clarity_effect) {
      const auto spatial_clarity_blur_timer = StageClock::now();
      blur_spatial_luminance_into(8, 0.0018, &clarity_blur);
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->spatial_clarity_blur_ms += elapsed_stage_ms(spatial_clarity_blur_timer);
      }
    }

    std::vector<float> haze_blur;
    if (has_dehaze_effect) {
      const auto spatial_haze_blur_timer = StageClock::now();
      blur_spatial_luminance_into(20, 0.0065, &haze_blur);
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->spatial_haze_blur_ms += elapsed_stage_ms(spatial_haze_blur_timer);
      }
    }

    const auto spatial_apply_timer = StageClock::now();
    const auto texture_gain = texture_strength >= 0.0 ? 1.30 : 0.48;
    const auto clarity_gain = clarity_strength >= 0.0 ? 1.08 : 0.46;
    const auto dehaze_gain = dehaze_strength >= 0.0 ? 0.95 : 0.36;
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto luminance = static_cast<double>(luminance_buffer[index]);
        auto local_activity = 0.0;
        if (x > 0) {
          local_activity = std::max(
              local_activity,
              std::abs(luminance - static_cast<double>(luminance_buffer[index - 1])));
        }
        if (x + 1 < width) {
          local_activity = std::max(
              local_activity,
              std::abs(luminance - static_cast<double>(luminance_buffer[index + 1])));
        }
        if (y > 0) {
          local_activity = std::max(
              local_activity,
              std::abs(luminance - static_cast<double>(luminance_buffer[index - safe_width])));
        }
        if (y + 1 < height) {
          local_activity = std::max(
              local_activity,
              std::abs(luminance - static_cast<double>(luminance_buffer[index + safe_width])));
        }
        const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
        const auto shadow_guard = smoothstep(0.025, 0.12, luminance);
        const auto highlight_headroom = 1.0 - smoothstep(0.74, 0.98, luminance);
        const auto tone_guard = shadow_guard * highlight_headroom;
        auto target_luminance = luminance;
        if (has_texture_effect) {
          const auto texture_signal =
              static_cast<double>(luminance_buffer[index] - texture_blur[index]);
          const auto broad_edge_signal =
              has_clarity_effect
                  ? static_cast<double>(luminance_buffer[index] - clarity_blur[index])
                  : local_activity;
          const auto broad_edge_guard =
              1.0 - smoothstep(0.08, 0.36, std::abs(broad_edge_signal)) * 0.72;
          target_luminance +=
              texture_strength * texture_signal * (0.46 + midtone_weight * 0.70) *
              broad_edge_guard * tone_guard * texture_gain;
        }
        if (has_clarity_effect) {
          const auto clarity_signal =
              static_cast<double>(luminance_buffer[index] - clarity_blur[index]);
          const auto edge_limit =
              1.0 - smoothstep(0.24, 0.58, std::abs(clarity_signal)) * 0.28;
          const auto structure_gate = smoothstep(0.003, 0.035, local_activity);
          target_luminance +=
              clarity_strength * clarity_signal * (0.36 + midtone_weight * 0.82) *
              tone_guard * edge_limit * structure_gate * clarity_gain;
        }
        if (has_dehaze_effect) {
          const auto haze_signal = std::clamp(
              static_cast<double>(luminance_buffer[index] - haze_blur[index]),
              -0.18,
              0.18);
          const auto haze_activity_gate = smoothstep(0.0015, 0.018, local_activity);
          const auto haze_edge_guard =
              1.0 - smoothstep(0.11, 0.18, std::abs(haze_signal)) * 0.95;
          target_luminance +=
              dehaze_strength * haze_signal * (0.52 + midtone_weight * 0.78) *
              highlight_headroom * haze_activity_gate * haze_edge_guard * dehaze_gain;
          const auto haze_shadow_weight = 1.0 - smoothstep(0.08, 0.20, luminance);
          const auto haze_structure_weight =
              smoothstep(0.008, 0.065, std::abs(haze_signal)) *
              haze_activity_gate * haze_edge_guard;
          if (dehaze_strength > 0.0) {
            target_luminance *= std::max(
                0.0,
                1.0 - dehaze_strength * haze_shadow_weight * haze_structure_weight * 0.52);
            target_luminance *= std::max(
                0.0,
                1.0 - dehaze_strength * haze_shadow_weight * haze_activity_gate * haze_edge_guard * 0.035);
          } else {
            target_luminance += (-dehaze_strength) * haze_shadow_weight * haze_structure_weight * 0.075;
          }
        }
        (*pixels)[index] = scale_pixel_to_luminance_with_headroom(
            (*pixels)[index],
            target_luminance,
            final_output_headroom,
            color_context);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->spatial_apply_ms += elapsed_stage_ms(spatial_apply_timer);
    }
    if (telemetry != nullptr) {
      telemetry->spatial_ms += elapsed_stage_ms(spatial_timer);
    }
    }
  }

  const auto post_color_timer = StageClock::now();
  const auto post_color_has_per_channel_mixer = has_per_channel_color_mixer_adjustments(state);
  const auto post_color_has_legacy_mixer = has_legacy_color_mixer_adjustments(state);
  const auto post_color_has_color_replacement = has_color_replacement_adjustments(state);
  const auto post_color_has_color_harmony = has_color_harmony_adjustments(state);
  const auto post_color_has_color_grading = has_color_grading_adjustments(state);
  const auto post_color_has_saturation_like =
      std::abs(saturation_strength) > 1e-9 || std::abs(vibrance_strength) > 1e-9 ||
      std::abs(dehaze_strength) > 1e-9;
  const auto post_color_has_mixer =
      post_color_has_per_channel_mixer || post_color_has_legacy_mixer;
  const auto post_color_has_hue_domain_adjustments =
      post_color_has_mixer || post_color_has_color_replacement ||
      post_color_has_color_harmony || post_color_has_color_grading;
  const auto post_color_has_adjustments =
      post_color_has_saturation_like || post_color_has_hue_domain_adjustments;
  const auto post_color_use_legacy_mixer =
      post_color_has_legacy_mixer && !post_color_has_per_channel_mixer;
  const auto post_color_mixer_model =
      post_color_has_mixer ? normalized_color_mixer_model(state) : std::string_view{"OKLab"};
  auto ran_post_color_on_gpu = false;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  {
    auto& metal = metal_backend();
    ran_post_color_on_gpu =
        metal.available() && metal_color_pipeline_supported(state) &&
        metal.apply_post_color_effects(pixels, width, height, state);
    if (ran_post_color_on_gpu && telemetry != nullptr) {
      telemetry->gpu_command_buffer_count += 1;
      telemetry->gpu_kernel_dispatch_count += 1;
      telemetry->gpu_cpu_to_gpu_bytes +=
          static_cast<std::int64_t>(pixels->size() * sizeof(ColorPixel));
      telemetry->gpu_gpu_to_cpu_bytes +=
          static_cast<std::int64_t>(pixels->size() * sizeof(ColorPixel));
    }
  }
#endif

  if (!ran_post_color_on_gpu) {
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
    const auto index = row_offset + static_cast<std::size_t>(x);
    auto& pixel = (*pixels)[index];
    if (master_curve.points.size() >= 2) {
      pixel = apply_master_rgb_point_curve(pixel, master_curve);
    }
    if (luma_curve.points.size() >= 2) {
      pixel = apply_luminance_point_curve(
          pixel,
          luma_curve,
          final_output_headroom,
          color_context);
    }
    if (red_curve.points.size() >= 2) {
      pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, red_curve));
    }
    if (green_curve.points.size() >= 2) {
      pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, green_curve));
    }
    if (blue_curve.points.size() >= 2) {
      pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, blue_curve));
    }
    if (has_rgb_channel_mixer_adjustments(state)) {
      pixel = apply_rgb_channel_mixer(pixel, state.rgb_channel_mixer);
    }
    if (state.black_and_white_enabled) {
      pixel = apply_black_and_white_mixer(pixel, state, color_context);
    }

    const auto highlight_chroma_enabled = highlight_chroma_compression_enabled(
        state,
        pixel,
        final_output_headroom);
    if (!post_color_has_adjustments && !highlight_chroma_enabled) {
      continue;
    }

    const auto channel_spread = std::max({
        std::abs(pixel.red - pixel.green),
        std::abs(pixel.green - pixel.blue),
        std::abs(pixel.red - pixel.blue),
    });

    auto oklab = linear_rgb_to_oklab(pixel, color_context);
    auto lightness = clamp_oklab_lightness(oklab.lightness, color_context);
    auto chroma = std::hypot(oklab.a, oklab.b);
    auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));
    const auto highlight_chroma_scale = highlight_chroma_enabled
                                            ? highlight_chroma_compression_scale(lightness, chroma)
                                            : 1.0;
    if (channel_spread <= 1e-6 && !post_color_has_hue_domain_adjustments &&
        highlight_chroma_scale >= 0.999999) {
      continue;
    }
    if (chroma <= 1e-4) {
      chroma = 0.0;
      hue = 0.0;
    }

    auto explicit_skin_protection = 1.0;
    auto replacement_luminance_reference = -1.0;
    auto replacement_luminance_weight = 0.0;
    if (chroma > 1e-6) {
      const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
      const auto mixer_hue_for_selection =
          post_color_has_mixer ? mixer_selection_hue(pixel, hue, chroma, post_color_mixer_model) : -1.0;
      const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
      const auto mid_lightness_weight = 1.0 - smoothstep(0.18, 0.48, std::abs(lightness - 0.56));
      const auto tonal_protection = tone_chroma_protection(lightness);
      explicit_skin_protection = skin_tone_protection_scale(state, normalized_hue, chroma);
      const auto skin_protection =
          default_skin_chroma_protection(normalized_hue, chroma) *
          explicit_skin_protection;
      chroma *= std::max(0.0, 1.0 + vibrance_strength * low_chroma_weight * mid_lightness_weight *
                                          tonal_protection * skin_protection * 0.82);
      chroma *= std::max(0.0, 1.0 + std::max(0.0, dehaze_strength) * 0.10);
      chroma *= highlight_chroma_scale;

      if (post_color_has_mixer && mixer_hue_for_selection >= 0.0) {
        const auto hue_scale = post_color_mixer_model == "HSV" ? 0.16 : 0.22;
        const auto saturation_scale =
            post_color_mixer_model == "LCH" ? 0.72 : (post_color_mixer_model == "HSV" ? 0.92 : 1.15);
        const auto luminance_scale =
            post_color_mixer_model == "HSV" ? 0.14 : (post_color_mixer_model == "LCH" ? 0.20 : 0.30);
        for (int ch = 0; ch < kColorMixerChannelCount; ++ch) {
          auto mixer = state.color_mixer_channels[static_cast<std::size_t>(ch)];
          if (post_color_use_legacy_mixer &&
              state.color_mixer_channel == kColorMixerChannelNames[static_cast<std::size_t>(ch)]) {
            mixer.hue = state.hue_adjustment;
            mixer.saturation = state.saturation_adjustment;
            mixer.luminance = state.luminance_adjustment;
          }
          const auto mixer_weight = channel_weight_for_selection(
              mixer_hue_for_selection, kColorMixerChannelNames[static_cast<std::size_t>(ch)]);
          if (mixer_weight > 0.0) {
            const auto protected_mixer_weight = mixer_weight * explicit_skin_protection;
            hue = wrap_radians(hue + protected_mixer_weight * (clamp_signed_percent(mixer.hue) / 100.0) *
                                          hue_scale * 2.0 * kPi);
            chroma *= std::max(
                0.0,
                1.0 + protected_mixer_weight * (clamp_signed_percent(mixer.saturation) / 100.0) * saturation_scale);
            lightness = clamp_unit(
                lightness + protected_mixer_weight * (clamp_signed_percent(mixer.luminance) / 100.0) * luminance_scale);
            apply_selective_color_adjustment(mixer, protected_mixer_weight, &lightness, &chroma, &hue);
          }
        }
      }
      if (post_color_has_color_replacement) {
        const auto selection_hue = rgb_hue_for_pixel(pixel);
        const auto range = std::clamp(state.color_replacement_range / 100.0, 0.01, 1.0) * 0.50;
        const auto softness = clamp_unit(state.color_replacement_softness / 100.0);
        const auto replacement_weight =
            (selection_hue >= 0.0
                 ? circular_feathered_selection_weight(
                       selection_hue,
                       wrap_unit(state.color_replacement_source_hue),
                       range,
                       softness)
                 : 0.0) *
            clamp_unit(state.color_replacement_strength / 100.0) * explicit_skin_protection;
        if (replacement_weight > 1e-6) {
          if (state.color_replacement_preserve_luminance) {
            replacement_luminance_reference =
                luminance_for_pixel(gamut_fit_oklch(lightness, chroma, hue, pixel.alpha, color_context), color_context);
            replacement_luminance_weight = replacement_weight;
          }
          const auto target_hue = oklab_hue_for_rgb_hue(state.color_replacement_target_hue, color_context);
          hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * replacement_weight);
          if (!state.color_replacement_preserve_saturation) {
            chroma = std::max(chroma, std::lerp(chroma, 0.16, replacement_weight));
          }
        }
      }
      if (post_color_has_color_harmony) {
        const auto normalized_hue = wrap_unit(hue / (2.0 * kPi));
        const auto target_hue = nearest_color_harmony_hue(state, normalized_hue) * 2.0 * kPi;
        const auto harmony_weight =
            clamp_unit(state.color_harmony_strength / 100.0) * 0.65 * explicit_skin_protection;
        hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * harmony_weight);
      }
    }

    if (post_color_has_color_grading) {
      auto grade_a = chroma * std::cos(hue);
      auto grade_b = chroma * std::sin(hue);

      const auto grading_amount = clamp_unit(state.grading_amount / 100.0);
      const auto apply_grade = [&](double grade_hue, double grade_sat, double grade_lum, double weight) {
        if (weight < 1e-9) {
          return;
        }
        const auto sat_norm = clamp_signed_percent(grade_sat) / 100.0;
        const auto hue_rad = wrap_unit(grade_hue) * 2.0 * kPi;
        const auto lum_norm = clamp_signed_percent(grade_lum) / 100.0;
        const auto scaled_weight = weight * grading_amount;
        const auto intensity = std::abs(sat_norm) * 0.18 * scaled_weight * explicit_skin_protection;
        if (intensity > 1e-9) {
          grade_a += intensity * std::cos(hue_rad);
          grade_b += intensity * std::sin(hue_rad);
        }
        lightness = clamp_unit(lightness + lum_norm * 0.20 * scaled_weight * explicit_skin_protection);
      };

      const auto balance_shift = clamp_signed_percent(state.grading_balance) / 100.0 * 0.18;
      const auto shadow_weight = 1.0 - smoothstep(0.15 - balance_shift, 0.45 - balance_shift, lightness);
      const auto highlight_weight = smoothstep(0.55 - balance_shift, 0.85 - balance_shift, lightness);
      const auto midtone_weight = 1.0 - shadow_weight - highlight_weight;

      apply_grade(state.grading_shadows_hue, state.grading_shadows_saturation,
                  state.grading_shadows_luminance, shadow_weight);
      apply_grade(state.grading_midtones_hue, state.grading_midtones_saturation,
                  state.grading_midtones_luminance, std::max(0.0, midtone_weight));
      apply_grade(state.grading_highlights_hue, state.grading_highlights_saturation,
                  state.grading_highlights_luminance, highlight_weight);

      chroma = std::hypot(grade_a, grade_b);
      hue = wrap_radians(std::atan2(grade_b, grade_a));
    }

    // Vibrance protects source image color before hue-domain tools. Saturation is
    // intentionally final so it controls chroma introduced by color grading too.
    if (std::abs(saturation_strength) > 1e-9) {
      chroma *= saturation_strength >= 0.0
                    ? std::max(0.0, 1.0 + saturation_strength * 0.85)
                    : std::max(0.0, 1.0 + saturation_strength);
    }

    pixel = gamut_fit_oklch(lightness, chroma, hue, pixel.alpha, color_context);
    if (replacement_luminance_reference >= 0.0 && replacement_luminance_weight > 1e-6) {
      const auto current_luminance = luminance_for_pixel(pixel, color_context);
      const auto target_luminance = std::lerp(
          current_luminance,
          std::clamp(replacement_luminance_reference, 0.0, final_output_headroom),
          clamp_unit(replacement_luminance_weight));
      pixel = scale_pixel_to_luminance(pixel, target_luminance, color_context);
    }
    const auto grading_adds_chroma =
        std::abs(state.grading_shadows_saturation) > 1e-9 ||
        std::abs(state.grading_midtones_saturation) > 1e-9 ||
        std::abs(state.grading_highlights_saturation) > 1e-9;
    if (saturation_strength <= -0.999999 && !grading_adds_chroma &&
        !has_color_replacement_adjustments(state)) {
      const auto neutral = static_cast<float>(std::clamp(
          luminance_for_pixel(pixel, color_context),
          0.0,
          final_output_headroom));
      pixel.red = neutral;
      pixel.green = neutral;
      pixel.blue = neutral;
    }
    }
  });
  if (cancellation.cancelled()) {
    return;
  }
  }
  if (telemetry != nullptr) {
    telemetry->post_color_ms += elapsed_stage_ms(post_color_timer);
  }
  }  // end slow path (else)

  const auto retouch_timer = StageClock::now();
  if (has_local_clone) {
    if (cancellation.cancelled()) {
      return;
    }
    apply_local_clone(pixels, width, height, color_context, effective_retouch_masks);
    if (cancellation.cancelled()) {
      return;
    }
  }

  if (has_local_red_eye) {
    if (cancellation.cancelled()) {
      return;
    }
    apply_local_red_eye(pixels, width, height, color_context, effective_retouch_masks);
    if (cancellation.cancelled()) {
      return;
    }
  }

  if (has_local_skin_cleanup) {
    if (cancellation.cancelled()) {
      return;
    }
    apply_local_skin_cleanup(pixels, width, height, color_context, effective_retouch_masks);
    if (cancellation.cancelled()) {
      return;
    }
  }

  if (has_local_frequency_smoothing) {
    if (cancellation.cancelled()) {
      return;
    }
    apply_local_frequency_smoothing(pixels, width, height, color_context, effective_retouch_masks);
    if (cancellation.cancelled()) {
      return;
    }
  }

  if (has_local_heal) {
    std::vector<double> heal_mask(static_cast<std::size_t>(width * height), 0.0);
    auto has_nonzero_heal = false;
    auto heal_texture_sum = 0.0;
    auto heal_color_sum = 0.0;
    auto heal_control_weight_sum = 0.0;
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        const auto heal = composite_local_heal(
            normalized_x, normalized_y, (*pixels)[index], color_context, effective_retouch_masks);
        heal_mask[index] = heal.weight;
        has_nonzero_heal = has_nonzero_heal || heal_mask[index] > 1e-4;
        if (heal.weight > 1e-4) {
          heal_texture_sum += heal.weight * heal.texture_blend;
          heal_color_sum += heal.weight * heal.color_blend;
          heal_control_weight_sum += heal.weight;
        }
      }
    }
    if (has_nonzero_heal) {
      const auto heal_texture_blend =
          heal_control_weight_sum > 1e-6 ? heal_texture_sum / heal_control_weight_sum : 0.68;
      const auto heal_color_blend =
          heal_control_weight_sum > 1e-6 ? heal_color_sum / heal_control_weight_sum : 0.46;
      apply_local_removal(
          pixels,
          width,
          height,
          heal_mask,
          0.35 + clamp_unit(heal_texture_blend) * 0.30,
          removal_quality,
          heal_texture_blend,
          heal_color_blend,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
    }
  }

  if (has_local_removal) {
    std::vector<double> removal_mask(static_cast<std::size_t>(width * height), 0.0);
    auto has_nonzero_removal = false;
    for (int y = 0; y < height; ++y) {
      if (cancellation.cancelled()) {
        return;
      }
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y * width + x);
        const auto normalized_x =
            width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y =
            height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        removal_mask[index] = composite_local_removal_weight(
            normalized_x, normalized_y, (*pixels)[index], color_context, effective_retouch_masks, "");
        has_nonzero_removal = has_nonzero_removal || removal_mask[index] > 1e-4;
      }
    }
    if (has_nonzero_removal) {
      apply_local_removal(
          pixels, width, height, removal_mask, 0.0, removal_quality, 1.0, 1.0, cancellation);
      if (cancellation.cancelled()) {
        return;
      }
    }
  }
  if (telemetry != nullptr) {
    telemetry->retouch_ms += elapsed_stage_ms(retouch_timer);
  }

  if (has_user_lut) {
    const auto user_lut_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        auto current = (*pixels)[index];
        for (const auto& runtime_lut : user_luts) {
          const auto source = current;
          const auto graded = apply_lut(runtime_lut.lut, source);
          current = ColorPixel{
              .red = static_cast<float>(
                  static_cast<double>(source.red) * (1.0 - runtime_lut.strength) +
                  static_cast<double>(graded.red) * runtime_lut.strength),
              .green = static_cast<float>(
                  static_cast<double>(source.green) * (1.0 - runtime_lut.strength) +
                  static_cast<double>(graded.green) * runtime_lut.strength),
              .blue = static_cast<float>(
                  static_cast<double>(source.blue) * (1.0 - runtime_lut.strength) +
                  static_cast<double>(graded.blue) * runtime_lut.strength),
              .alpha = source.alpha,
          };
        }
        (*pixels)[index] = current;
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->user_lut_ms += elapsed_stage_ms(user_lut_timer);
    }
  }

  // ── Detail pass: sharpening and noise reduction in OKLab space ──
  // Runs after both the fast (LUT) and slow (per-pixel) tone/color paths.
  if (has_detail_effects) {
    const auto detail_timer = StageClock::now();
    if (cancellation.cancelled()) {
      return;
    }
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
    auto& metal = metal_backend();
    if (metal.available() &&
        metal_color_pipeline_supported(state) &&
        metal.apply_detail_effects(
            pixels,
            width,
            height,
            sharpen_strength,
            sharpen_radius,
            sharpen_detail,
            sharpen_mask_strength,
            luma_nr_strength,
            color_nr_strength,
            telemetry)) {
      if (telemetry != nullptr) {
        telemetry->detail_ms += elapsed_stage_ms(detail_timer);
      }
      return;
    }
#endif

    // Convert to OKLab buffers for perceptual processing
    const auto pixel_count = static_cast<std::size_t>(width * height);
    std::vector<float> lightness_buf(pixel_count);
    std::vector<float> chroma_a_buf(pixel_count);
    std::vector<float> chroma_b_buf(pixel_count);
    BlurWorkspace detail_blur_workspace;

    const auto detail_oklab_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto ok = linear_rgb_to_oklab((*pixels)[index], color_context);
        lightness_buf[index] = static_cast<float>(ok.lightness);
        chroma_a_buf[index] = static_cast<float>(ok.a);
        chroma_b_buf[index] = static_cast<float>(ok.b);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->detail_oklab_ms += elapsed_stage_ms(detail_oklab_timer);
    }

    // Sharpening: unsharp mask on lightness with edge-aware masking. Radius
    // controls the blur scale; Detail increases fine-edge gain without
    // changing the user's amount.
    if (std::abs(sharpen_strength) > 1e-6) {
      const auto detail_sharpen_timer = StageClock::now();
      const auto sharp_radius = std::max(1, static_cast<int>(std::lround(sharpen_radius)));
      const auto edge_radius = std::max(sharp_radius + 2, static_cast<int>(std::lround(sharpen_radius * 3.0)));
      const auto sharpen_gain = 1.9 + sharpen_detail * 1.15;
      std::vector<float> sharp_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          sharp_radius,
          &sharp_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> edge_blur;
      if (sharpen_mask_strength > 1e-6) {
        gaussian_approximate_blur_into(
            lightness_buf,
            width,
            height,
            edge_radius,
            &edge_blur,
            &detail_blur_workspace,
            cancellation);
        if (cancellation.cancelled()) {
          return;
        }
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          const auto detail_signal = lightness_buf[index] - sharp_blur[index];
          auto mask_gate = 1.0;
          if (sharpen_mask_strength > 1e-6) {
            const auto edge_strength = std::abs(lightness_buf[index] - edge_blur[index]);
            mask_gate = smoothstep(
                0.0015,
                0.012 + 0.06 * sharpen_mask_strength,
                static_cast<double>(edge_strength));
          }
          lightness_buf[index] += static_cast<float>(
              static_cast<double>(detail_signal) * sharpen_strength * sharpen_gain * mask_gate);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_sharpen_ms += elapsed_stage_ms(detail_sharpen_timer);
      }
    }

    // Luminance noise reduction: edge-preserving blur on lightness.
    // Uses a box blur at radius 2 and blends toward it proportional to
    // strength, but preserves edges by reducing blend where local contrast
    // is high.
    if (std::abs(luma_nr_strength) > 1e-6) {
      const auto detail_luminance_nr_timer = StageClock::now();
      std::vector<float> nr_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          2,
          &nr_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> nr_edge_blur;
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          5,
          &nr_edge_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          const auto edge = std::abs(lightness_buf[index] - nr_edge_blur[index]);
          const auto edge_preserve = 1.0 - smoothstep(0.005, 0.04, static_cast<double>(edge));
          const auto blend = luma_nr_strength * 0.85 * edge_preserve;
          lightness_buf[index] = static_cast<float>(
              static_cast<double>(lightness_buf[index]) * (1.0 - blend) +
              static_cast<double>(nr_blur[index]) * blend);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_luminance_nr_ms += elapsed_stage_ms(detail_luminance_nr_timer);
      }
    }

    // Color noise reduction: edge-preserving blur on chrominance (a, b).
    // Chrominance noise is typically stronger than luminance noise, so
    // a wider radius and stronger blend are used.
    if (std::abs(color_nr_strength) > 1e-6) {
      const auto detail_color_nr_timer = StageClock::now();
      std::vector<float> a_blur;
      gaussian_approximate_blur_into(
          chroma_a_buf,
          width,
          height,
          3,
          &a_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }
      std::vector<float> b_blur;
      gaussian_approximate_blur_into(
          chroma_b_buf,
          width,
          height,
          3,
          &b_blur,
          &detail_blur_workspace,
          cancellation);
      if (cancellation.cancelled()) {
        return;
      }

      parallel_for(height, [&](int y) {
        if (cancellation.cancelled()) {
          return;
        }
        const auto row_offset = static_cast<std::size_t>(y) * safe_width;
        const auto blend = color_nr_strength * 0.90;
        for (int x = 0; x < width; ++x) {
          const auto index = row_offset + static_cast<std::size_t>(x);
          chroma_a_buf[index] = static_cast<float>(
              static_cast<double>(chroma_a_buf[index]) * (1.0 - blend) +
              static_cast<double>(a_blur[index]) * blend);
          chroma_b_buf[index] = static_cast<float>(
              static_cast<double>(chroma_b_buf[index]) * (1.0 - blend) +
              static_cast<double>(b_blur[index]) * blend);
        }
      });
      if (cancellation.cancelled()) {
        return;
      }
      if (telemetry != nullptr) {
        telemetry->detail_color_nr_ms += elapsed_stage_ms(detail_color_nr_timer);
      }
    }

    // Convert back from OKLab to linear RGB
    const auto detail_output_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto L =
            clamp_oklab_lightness(static_cast<double>(lightness_buf[index]), color_context);
        const auto a = static_cast<double>(chroma_a_buf[index]);
        const auto b = static_cast<double>(chroma_b_buf[index]);
        (*pixels)[index] = gamut_fit_oklab(L, a, b, (*pixels)[index].alpha, color_context);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->detail_output_ms += elapsed_stage_ms(detail_output_timer);
      telemetry->detail_ms += elapsed_stage_ms(detail_timer);
    }
  }

  const auto final_output_timer = StageClock::now();
  if (black_point_compensation_affects_output(state.color_pipeline)) {
    const auto final_output_black_point_timer = StageClock::now();
    parallel_for(height, [&](int y) {
      if (cancellation.cancelled()) {
        return;
      }
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        (*pixels)[index] = apply_black_point_compensation((*pixels)[index], state.color_pipeline);
      }
    });
    if (cancellation.cancelled()) {
      return;
    }
    if (telemetry != nullptr) {
      telemetry->final_output_black_point_ms += elapsed_stage_ms(final_output_black_point_timer);
    }
  }

  const auto final_output_headroom_timer = StageClock::now();
  parallel_for(height, [&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      (*pixels)[index] = fit_pixel_to_output_headroom((*pixels)[index], final_output_headroom);
    }
  });
  if (cancellation.cancelled()) {
    return;
  }
  if (telemetry != nullptr) {
    telemetry->final_output_headroom_ms += elapsed_stage_ms(final_output_headroom_timer);
    telemetry->final_output_ms += elapsed_stage_ms(final_output_timer);
  }
}

}  // namespace photo_editor::pipeline
