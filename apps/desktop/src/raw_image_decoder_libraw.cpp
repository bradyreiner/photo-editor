#include "raw_image_decoder_libraw.h"
#include "capture_camera_metadata.h"

#include <QtGui/qrgbafloat.h>

#include <QColorSpace>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <deque>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/dcp_profile.h"

#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
#include <libraw/libraw.h>
#endif

namespace photo_editor::desktop {

namespace {

constexpr auto kMaximumRawMemoryBytes = std::uint64_t{1536U} * 1024U * 1024U;

// Retain LibRaw's packed 16-bit samples before float conversion/resizing.
// Half/full demosaic modes remain separate: resampling a different mode would
// change pixels. The two-entry budget also bounds retained memory on navigation.
struct DecodedRawSamples {
  QString key;
  QByteArray pixels;
  int width = 0;
  int height = 0;
  photo_editor::pipeline::DecodedImageMetadata metadata;
};
struct RawSampleCache {
  QMutex mutex;
  std::deque<DecodedRawSamples> entries;
  LibRawDecodeCacheStats stats;
};
RawSampleCache& raw_sample_cache() {
  static RawSampleCache cache;
  return cache;
}
constexpr qint64 kRawSampleCacheBudget = 192 * 1024 * 1024;

struct RawMetadataCacheEntry final {
  QString key;
  photo_editor::pipeline::DecodedImageMetadata metadata;
};
struct RawMetadataCache final {
  QMutex mutex;
  std::deque<RawMetadataCacheEntry> entries;
  quint64 hits = 0;
  quint64 misses = 0;
};
RawMetadataCache& raw_metadata_cache() {
  static RawMetadataCache cache;
  return cache;
}
constexpr std::size_t kRawMetadataCacheCapacity = 64;

bool raw_metadata_cache_key(
    const QString& file_path,
    const RawDecodeSettings& settings,
    int output_contract,
    QString* key) {
  if (key == nullptr) {
    return false;
  }
  const QFileInfo info(file_path);
  const auto absolute_path = info.absoluteFilePath();
  if (absolute_path.isEmpty()) {
    return false;
  }

  auto file_revision = QString{};
#if defined(Q_OS_UNIX)
  const auto encoded_path = QFile::encodeName(absolute_path);
  struct stat status {};
  if (::stat(encoded_path.constData(), &status) != 0 ||
      !S_ISREG(status.st_mode)) {
    return false;
  }
#if defined(Q_OS_MACOS)
  const auto modified = status.st_mtimespec;
  const auto changed = status.st_ctimespec;
#else
  const auto modified = status.st_mtim;
  const auto changed = status.st_ctim;
#endif
  file_revision = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
      .arg(absolute_path)
      .arg(static_cast<qulonglong>(status.st_dev))
      .arg(static_cast<qulonglong>(status.st_ino))
      .arg(static_cast<qlonglong>(status.st_size))
      .arg(static_cast<qlonglong>(modified.tv_sec))
      .arg(static_cast<qlonglong>(modified.tv_nsec))
      .arg(static_cast<qlonglong>(changed.tv_sec))
      .arg(static_cast<qlonglong>(changed.tv_nsec));
#else
  if (!info.isFile()) {
    return false;
  }
  file_revision = QStringLiteral("%1|%2|%3|%4")
      .arg(absolute_path)
      .arg(info.size())
      .arg(info.lastModified().toMSecsSinceEpoch())
      .arg(info.metadataChangeTime().toMSecsSinceEpoch());
#endif

  // Metadata depends on settings as well as the source revision. Use enough
  // precision to keep distinct floating-point settings from sharing an entry.
  const auto settings_key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
      .arg(settings.input_transform_enabled ? 1 : 0)
      .arg(settings.demosaic_quality.trimmed().toLower())
      .arg(settings.exposure, 0, 'g', 17)
      .arg(settings.hot_dead_pixel_cleanup_amount, 0, 'g', 17)
      .arg(settings.false_color_suppression_amount, 0, 'g', 17)
      .arg(settings.highlight_recovery_enabled ? 1 : 0)
      .arg(settings.white_balance_override_enabled ? 1 : 0)
      .arg(settings.white_balance_temperature, 0, 'g', 17)
      .arg(settings.white_balance_tint, 0, 'g', 17);
  *key = QStringLiteral("%1|%2|%3|%4")
      .arg(file_revision, settings_key,
           settings.input_color_profile.trimmed().toLower())
      .arg(output_contract);
  return true;
}

enum class LibRawOutputContract {
  CameraLinear,
  CharacterizedLinearSrgb,
};

#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR

constexpr auto kMaximumDecodedPixels = std::uint64_t{200U} * 1000U * 1000U;

[[nodiscard]] double sanitized_baseline_exposure(float value) {
  // LibRaw uses -999 for an absent DNG BaselineExposure tag. Passing that
  // finite sentinel into the engine underflows the camera-linear image to
  // black, so admit only a physically meaningful exposure range.
  const auto exposure = static_cast<double>(value);
  return std::isfinite(exposure) && exposure >= -32.0 && exposure <= 32.0
             ? exposure
             : 0.0;
}

int cancellation_progress_callback(
    void* context,
    LibRaw_progress,
    int,
    int) {
  const auto* cancellation =
      static_cast<const photo_editor::pipeline::
                      CancellationToken*>(
          context);
  return cancellation != nullptr &&
                 cancellation->cancelled()
             ? 1
             : 0;
}

[[nodiscard]] std::array<double, 9> libraw_cam_xyz_matrix(
    const libraw_colordata_t& color) {
  // LibRaw stores cam_xyz as camera-channel rows by XYZ component. The engine
  // uses row-major matrices multiplying an RGB column vector, so transpose it
  // into XYZ rows by camera-channel columns.
  return {
      color.cam_xyz[0][0], color.cam_xyz[1][0], color.cam_xyz[2][0],
      color.cam_xyz[0][1], color.cam_xyz[1][1], color.cam_xyz[2][1],
      color.cam_xyz[0][2], color.cam_xyz[1][2], color.cam_xyz[2][2],
  };
}

[[nodiscard]] bool executable_camera_matrix(
    const std::array<double, 9>& matrix) {
  if (!std::all_of(matrix.cbegin(), matrix.cend(), [](double value) {
        return std::isfinite(value);
      })) {
    return false;
  }
  const auto determinant =
      matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
      matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
      matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
  return std::isfinite(determinant) && std::abs(determinant) >= 1.0e-12;
}

struct DecoderCameraCharacterization final {
  bool valid = false;
  std::array<double, 9> camera_to_xyz{};
  std::string reference_white;
};

[[nodiscard]] bool interpolate_white_balance(const libraw_colordata_t& color,
                                             double temperature, double tint,
                                             float* user_multipliers);

[[nodiscard]] std::array<double, 9> libraw_camera_to_linear_srgb_matrix(
    const libraw_colordata_t& color) {
  return {
      color.rgb_cam[0][0], color.rgb_cam[0][1], color.rgb_cam[0][2],
      color.rgb_cam[1][0], color.rgb_cam[1][1], color.rgb_cam[1][2],
      color.rgb_cam[2][0], color.rgb_cam[2][1], color.rgb_cam[2][2],
  };
}

[[nodiscard]] std::optional<std::array<double, 3>>
camera_white_balance_gains(const libraw_colordata_t& color,
                           double temperature,
                           double tint,
                           bool override_enabled) {
  if (override_enabled) {
    auto multipliers = std::array<float, 4>{};
    if (!interpolate_white_balance(
            color, temperature, tint, multipliers.data())) {
      return std::nullopt;
    }
    return std::array<double, 3>{
        multipliers[0], multipliers[1], multipliers[2]};
  }

  const auto neutral = std::array<double, 3>{
      color.dng_levels.asshotneutral[0],
      color.dng_levels.asshotneutral[1],
      color.dng_levels.asshotneutral[2],
  };
  if (std::all_of(neutral.cbegin(), neutral.cend(), [](double value) {
        return std::isfinite(value) && value > 0.0;
      })) {
    const auto green_gain = 1.0 / neutral[1];
    return std::array<double, 3>{
        (1.0 / neutral[0]) / green_gain,
        1.0,
        (1.0 / neutral[2]) / green_gain,
    };
  }

  const auto green =
      0.5 * static_cast<double>(color.cam_mul[1] + color.cam_mul[3]);
  if (!std::isfinite(green) || green <= 0.0 ||
      !std::isfinite(color.cam_mul[0]) || color.cam_mul[0] <= 0.0F ||
      !std::isfinite(color.cam_mul[2]) || color.cam_mul[2] <= 0.0F) {
    return std::nullopt;
  }
  return std::array<double, 3>{
      static_cast<double>(color.cam_mul[0]) / green,
      1.0,
      static_cast<double>(color.cam_mul[2]) / green,
  };
}

[[nodiscard]] std::array<double, 9> libraw_dng_matrix(
    const float matrix[4][3]) {
  return {
      matrix[0][0], matrix[0][1], matrix[0][2],
      matrix[1][0], matrix[1][1], matrix[1][2],
      matrix[2][0], matrix[2][1], matrix[2][2],
  };
}

[[nodiscard]] std::array<double, 9> libraw_dng_forward_matrix(
    const float matrix[3][4]) {
  return {
      matrix[0][0], matrix[0][1], matrix[0][2],
      matrix[1][0], matrix[1][1], matrix[1][2],
      matrix[2][0], matrix[2][1], matrix[2][2],
  };
}

[[nodiscard]] std::array<double, 9> libraw_dng_calibration_matrix(
    const float matrix[4][4]) {
  return {
      matrix[0][0], matrix[0][1], matrix[0][2],
      matrix[1][0], matrix[1][1], matrix[1][2],
      matrix[2][0], matrix[2][1], matrix[2][2],
  };
}

[[nodiscard]] DecoderCameraCharacterization
resolve_decoder_camera_characterization(
    const libraw_colordata_t& color,
    double source_temperature,
    double source_tint,
    bool white_balance_override_enabled) {
  const auto camera_to_linear_srgb =
      libraw_camera_to_linear_srgb_matrix(color);
  const auto white_balance = camera_white_balance_gains(
      color,
      source_temperature,
      source_tint,
      white_balance_override_enabled);
  if (executable_camera_matrix(camera_to_linear_srgb) &&
      white_balance.has_value()) {
    auto characterized = photo_editor::pipeline::ColorMatrix3{
        .values = camera_to_linear_srgb};
    for (std::size_t row = 0U; row < 3U; ++row) {
      for (std::size_t column = 0U; column < 3U; ++column) {
        characterized.values[row * 3U + column] *= (*white_balance)[column];
      }
    }
    const auto camera_to_xyz = photo_editor::pipeline::multiply_matrix3(
        photo_editor::pipeline::rgb_to_xyz_matrix(
            photo_editor::pipeline::make_linear_srgb_color_space()),
        characterized);
    if (executable_camera_matrix(camera_to_xyz.values)) {
      return DecoderCameraCharacterization{
          .valid = true,
          .camera_to_xyz = camera_to_xyz.values,
          .reference_white = "D65",
      };
    }
  }

  auto profile = photo_editor::pipeline::DcpProfile{};
  const auto first_color = libraw_dng_matrix(color.dng_color[0].colormatrix);
  if (executable_camera_matrix(first_color) &&
      color.dng_color[0].illuminant != 0U) {
    profile.color_matrix1 = first_color;
    profile.calibration_illuminant1 = color.dng_color[0].illuminant;
    const auto first_forward =
        libraw_dng_forward_matrix(color.dng_color[0].forwardmatrix);
    if (executable_camera_matrix(first_forward)) {
      profile.forward_matrix1 = first_forward;
    }
    const auto first_calibration =
        libraw_dng_calibration_matrix(color.dng_color[0].calibration);
    if (executable_camera_matrix(first_calibration)) {
      profile.camera_calibration1 = first_calibration;
    }

    const auto second_color =
        libraw_dng_matrix(color.dng_color[1].colormatrix);
    if (executable_camera_matrix(second_color) &&
        color.dng_color[1].illuminant != 0U) {
      profile.color_matrix2 = second_color;
      profile.calibration_illuminant2 = color.dng_color[1].illuminant;
      const auto second_forward =
          libraw_dng_forward_matrix(color.dng_color[1].forwardmatrix);
      if (executable_camera_matrix(second_forward)) {
        profile.forward_matrix2 = second_forward;
      }
      const auto second_calibration =
          libraw_dng_calibration_matrix(color.dng_color[1].calibration);
      if (executable_camera_matrix(second_calibration)) {
        profile.camera_calibration2 = second_calibration;
      }
    }

    const auto transform = photo_editor::pipeline::dcp_camera_to_xyz_d50(
        profile,
        source_temperature,
        source_tint);
    if (transform.valid &&
        executable_camera_matrix(transform.camera_to_xyz_d50)) {
      return DecoderCameraCharacterization{
          .valid = true,
          .camera_to_xyz = transform.camera_to_xyz_d50,
          .reference_white = "D50",
      };
    }
  }

  const auto cam_xyz = libraw_cam_xyz_matrix(color);
  if (executable_camera_matrix(cam_xyz)) {
    return DecoderCameraCharacterization{
        .valid = true,
        .camera_to_xyz = cam_xyz,
        .reference_white = "D65",
    };
  }
  return {};
}

[[nodiscard]] std::string normalized_version(std::string_view version) {
  const auto separator = version.find('-');
  return std::string(version.substr(0U, separator));
}

[[nodiscard]] bool uses_draft_demosaic(const RawDecodeSettings& settings,
                                       bool scaled_preview) {
  (void)scaled_preview;
  const auto quality = settings.demosaic_quality.trimmed();
  if (quality.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (quality.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return false;
  }
  // Resolution bounding is independent from RAW rendering fidelity. Balanced
  // previews keep the same demosaic choice as the final decode so a scaled
  // preview cannot introduce a temporary camera-color interpretation.
  return false;
}

struct LibRawDeleter final {
  void operator()(libraw_data_t* raw) const {
    if (raw != nullptr) {
      libraw_close(raw);
    }
  }
};

struct LibRawImageDeleter final {
  void operator()(libraw_processed_image_t* image) const {
    if (image != nullptr) {
      libraw_dcraw_clear_mem(image);
    }
  }
};

using LibRawHandle = std::unique_ptr<libraw_data_t, LibRawDeleter>;
using LibRawImage =
    std::unique_ptr<libraw_processed_image_t, LibRawImageDeleter>;

[[nodiscard]] LibRawHandle open_raw(const QString& file_path) {
  const QFileInfo info(file_path);
  if (!info.exists() || !info.isFile() || !info.isReadable()) {
    return {};
  }
  auto raw = LibRawHandle(libraw_init(0U));
  if (raw == nullptr) {
    return {};
  }
  raw->rawparams.max_raw_memory_mb =
      static_cast<unsigned>(kMaximumRawMemoryBytes / (1024U * 1024U));
  const auto encoded_path = file_path.toUtf8();
  if (libraw_open_file(raw.get(), encoded_path.constData()) != LIBRAW_SUCCESS) {
    return {};
  }
  return raw;
}

[[nodiscard]] std::optional<std::pair<double, double>>
estimate_dng_as_shot_white_balance(const libraw_colordata_t& color) {
  auto neutral = std::array<double, 3>{
      color.dng_levels.asshotneutral[0],
      color.dng_levels.asshotneutral[1],
      color.dng_levels.asshotneutral[2],
  };
  if (std::any_of(neutral.cbegin(), neutral.cend(), [](double value) {
        return !std::isfinite(value) || value <= 0.0;
      })) {
    const auto green =
        0.5 * static_cast<double>(color.cam_mul[1] + color.cam_mul[3]);
    if (!std::isfinite(green) || green <= 0.0 ||
        !std::isfinite(color.cam_mul[0]) || color.cam_mul[0] <= 0.0F ||
        !std::isfinite(color.cam_mul[2]) || color.cam_mul[2] <= 0.0F) {
      return std::nullopt;
    }
    neutral = {
        green / static_cast<double>(color.cam_mul[0]),
        1.0,
        green / static_cast<double>(color.cam_mul[2]),
    };
  }

  const auto first_matrix = libraw_dng_matrix(color.dng_color[0].colormatrix);
  const auto first_temperature =
      photo_editor::pipeline::dcp_calibration_illuminant_temperature(
          color.dng_color[0].illuminant);
  if (!executable_camera_matrix(first_matrix) ||
      !first_temperature.has_value()) {
    return std::nullopt;
  }
  const auto second_matrix = libraw_dng_matrix(color.dng_color[1].colormatrix);
  const auto second_temperature =
      photo_editor::pipeline::dcp_calibration_illuminant_temperature(
          color.dng_color[1].illuminant);
  const auto dual = executable_camera_matrix(second_matrix) &&
                    second_temperature.has_value();

  auto result = std::pair<double, double>{5200.0, 0.0};
  for (auto iteration = 0; iteration < 12; ++iteration) {
    const auto weight = dual
                            ? photo_editor::pipeline::
                                  dcp_reciprocal_temperature_weight(
                                      result.first,
                                      *first_temperature,
                                      *second_temperature)
                            : 0.0;
    auto matrix = photo_editor::pipeline::ColorMatrix3{};
    for (std::size_t index = 0U; index < matrix.values.size(); ++index) {
      matrix.values[index] =
          first_matrix[index] * (1.0 - weight) + second_matrix[index] * weight;
    }
    const auto camera_to_xyz = photo_editor::pipeline::invert_matrix3(matrix);
    const auto xyz = photo_editor::pipeline::multiply_matrix3_vector(
        camera_to_xyz, neutral);
    const auto sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || sum <= 1.0e-12) {
      return std::nullopt;
    }
    const auto resolved =
        photo_editor::pipeline::dcp_chromaticity_to_temperature_tint(
            photo_editor::pipeline::DcpChromaticity{
                .x = xyz[0] / sum,
                .y = xyz[1] / sum,
            });
    if (!resolved.has_value()) {
      return std::nullopt;
    }
    const auto next = std::pair<double, double>{
        std::clamp(resolved->first, 2000.0, 12000.0),
        std::clamp(resolved->second, -150.0, 150.0),
    };
    if (std::abs(next.first - result.first) < 0.01 &&
        std::abs(next.second - result.second) < 0.001) {
      return next;
    }
    result = next;
  }
  return result;
}

[[nodiscard]] std::pair<double, double> estimate_as_shot_white_balance(
    const libraw_colordata_t& color) {
  const auto green =
      0.5 * static_cast<double>(color.cam_mul[1] + color.cam_mul[3]);
  if (!std::isfinite(green) || green <= 0.0 ||
      !std::isfinite(color.cam_mul[0]) || color.cam_mul[0] <= 0.0F ||
      !std::isfinite(color.cam_mul[2]) || color.cam_mul[2] <= 0.0F) {
    return estimate_dng_as_shot_white_balance(color).value_or(
        std::pair<double, double>{5200.0, 0.0});
  }
  const auto target_red = static_cast<double>(color.cam_mul[0]) / green;
  const auto target_blue = static_cast<double>(color.cam_mul[2]) / green;
  auto best_temperature = 5200.0;
  auto best_error = std::numeric_limits<double>::infinity();
  for (const auto& row : color.WBCT_Coeffs) {
    const auto temperature = static_cast<double>(row[0]);
    const auto row_green = 0.5 * static_cast<double>(row[2] + row[4]);
    if (!std::isfinite(temperature) || temperature < 1000.0 ||
        temperature > 50000.0 || !std::isfinite(row_green) ||
        row_green <= 0.0 || row[1] <= 0.0F || row[3] <= 0.0F) {
      continue;
    }
    const auto red = static_cast<double>(row[1]) / row_green;
    const auto blue = static_cast<double>(row[3]) / row_green;
    const auto error = std::pow(std::log(red / target_red), 2.0) +
                       std::pow(std::log(blue / target_blue), 2.0);
    if (error < best_error) {
      best_error = error;
      best_temperature = temperature;
    }
  }
  if (std::isfinite(best_error)) {
    return {std::clamp(best_temperature, 2000.0, 12000.0), 0.0};
  }
  return estimate_dng_as_shot_white_balance(color).value_or(
      std::pair<double, double>{5200.0, 0.0});
}

[[nodiscard]] bool interpolate_white_balance(const libraw_colordata_t& color,
                                             double temperature, double tint,
                                             float* user_multipliers) {
  if (user_multipliers == nullptr) {
    return false;
  }
  const float* lower = nullptr;
  const float* upper = nullptr;
  for (const auto& row : color.WBCT_Coeffs) {
    const auto row_temperature = static_cast<double>(row[0]);
    if (!std::isfinite(row_temperature) || row_temperature < 1000.0 ||
        row_temperature > 50000.0 || row[1] <= 0.0F || row[2] <= 0.0F ||
        row[3] <= 0.0F) {
      continue;
    }
    if (row_temperature <= temperature &&
        (lower == nullptr || row[0] > lower[0])) {
      lower = row;
    }
    if (row_temperature >= temperature &&
        (upper == nullptr || row[0] < upper[0])) {
      upper = row;
    }
  }
  if (lower == nullptr) {
    lower = upper;
  }
  if (upper == nullptr) {
    upper = lower;
  }
  if (lower == nullptr || upper == nullptr) {
    const auto xy =
        photo_editor::pipeline::dcp_temperature_tint_to_xy(temperature, tint);
    if (!xy.valid()) {
      return false;
    }
    const std::array<double, 3> white_xyz = {
        xy.x / xy.y,
        1.0,
        (1.0 - xy.x - xy.y) / xy.y,
    };
    std::array<double, 3> camera_white{};
    auto dng_matrix_valid = true;
    for (std::size_t camera = 0U; camera < camera_white.size(); ++camera) {
      for (std::size_t xyz = 0U; xyz < white_xyz.size(); ++xyz) {
        camera_white[camera] +=
            static_cast<double>(color.dng_color[0].colormatrix[camera][xyz]) *
            white_xyz[xyz];
      }
      dng_matrix_valid = dng_matrix_valid &&
                         std::isfinite(camera_white[camera]) &&
                         camera_white[camera] > 0.0;
    }
    if (!dng_matrix_valid) {
      const std::array<double, 9> camera_to_xyz = {
          color.cam_xyz[0][0], color.cam_xyz[1][0], color.cam_xyz[2][0],
          color.cam_xyz[0][1], color.cam_xyz[1][1], color.cam_xyz[2][1],
          color.cam_xyz[0][2], color.cam_xyz[1][2], color.cam_xyz[2][2],
      };
      const auto determinant =
          camera_to_xyz[0] * (camera_to_xyz[4] * camera_to_xyz[8] -
                              camera_to_xyz[5] * camera_to_xyz[7]) -
          camera_to_xyz[1] * (camera_to_xyz[3] * camera_to_xyz[8] -
                              camera_to_xyz[5] * camera_to_xyz[6]) +
          camera_to_xyz[2] * (camera_to_xyz[3] * camera_to_xyz[7] -
                              camera_to_xyz[4] * camera_to_xyz[6]);
      if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12) {
        return false;
      }
      const auto reciprocal = 1.0 / determinant;
      const std::array<double, 9> xyz_to_camera = {
          (camera_to_xyz[4] * camera_to_xyz[8] -
           camera_to_xyz[5] * camera_to_xyz[7]) *
              reciprocal,
          (camera_to_xyz[2] * camera_to_xyz[7] -
           camera_to_xyz[1] * camera_to_xyz[8]) *
              reciprocal,
          (camera_to_xyz[1] * camera_to_xyz[5] -
           camera_to_xyz[2] * camera_to_xyz[4]) *
              reciprocal,
          (camera_to_xyz[5] * camera_to_xyz[6] -
           camera_to_xyz[3] * camera_to_xyz[8]) *
              reciprocal,
          (camera_to_xyz[0] * camera_to_xyz[8] -
           camera_to_xyz[2] * camera_to_xyz[6]) *
              reciprocal,
          (camera_to_xyz[2] * camera_to_xyz[3] -
           camera_to_xyz[0] * camera_to_xyz[5]) *
              reciprocal,
          (camera_to_xyz[3] * camera_to_xyz[7] -
           camera_to_xyz[4] * camera_to_xyz[6]) *
              reciprocal,
          (camera_to_xyz[1] * camera_to_xyz[6] -
           camera_to_xyz[0] * camera_to_xyz[7]) *
              reciprocal,
          (camera_to_xyz[0] * camera_to_xyz[4] -
           camera_to_xyz[1] * camera_to_xyz[3]) *
              reciprocal,
      };
      for (std::size_t row = 0U; row < camera_white.size(); ++row) {
        camera_white[row] = xyz_to_camera[row * 3U] * white_xyz[0] +
                            xyz_to_camera[row * 3U + 1U] * white_xyz[1] +
                            xyz_to_camera[row * 3U + 2U] * white_xyz[2];
        if (!std::isfinite(camera_white[row]) || camera_white[row] <= 0.0) {
          return false;
        }
      }
    }
    const auto green = camera_white[1];
    const std::array<double, 4> multipliers = {
        green / camera_white[0],
        1.0,
        green / camera_white[2],
        1.0,
    };
    for (std::size_t channel = 0U; channel < multipliers.size(); ++channel) {
      if (!std::isfinite(multipliers[channel]) || multipliers[channel] <= 0.0 ||
          multipliers[channel] > 128.0) {
        return false;
      }
      user_multipliers[channel] = static_cast<float>(multipliers[channel]);
    }
    return true;
  }
  auto weight = 0.0;
  if (lower != upper && lower[0] != upper[0]) {
    const auto reciprocal_lower = 1.0 / static_cast<double>(lower[0]);
    const auto reciprocal_upper = 1.0 / static_cast<double>(upper[0]);
    weight = (1.0 / temperature - reciprocal_lower) /
             (reciprocal_upper - reciprocal_lower);
    weight = std::clamp(weight, 0.0, 1.0);
  }
  std::array<double, 4> multipliers{};
  for (std::size_t channel = 0U; channel < multipliers.size(); ++channel) {
    const auto lower_value = static_cast<double>(lower[channel + 1U]);
    const auto upper_value = static_cast<double>(upper[channel + 1U]);
    multipliers[channel] = lower_value + (upper_value - lower_value) * weight;
  }
  if (multipliers[3] <= 0.0) {
    multipliers[3] = multipliers[1];
  }
  const auto green = 0.5 * (multipliers[1] + multipliers[3]);
  if (!std::isfinite(green) || green <= 0.0) {
    return false;
  }
  const auto green_tint_scale =
      std::exp2(-std::clamp(tint, -150.0, 150.0) / 150.0);
  multipliers[1] *= green_tint_scale;
  multipliers[3] *= green_tint_scale;
  const auto normalization = 0.5 * (multipliers[1] + multipliers[3]);
  for (std::size_t channel = 0U; channel < multipliers.size(); ++channel) {
    const auto value = multipliers[channel] / normalization;
    if (!std::isfinite(value) || value <= 0.0 || value > 128.0) {
      return false;
    }
    user_multipliers[channel] = static_cast<float>(value);
  }
  return true;
}

void populate_metadata(const libraw_data_t& raw,
                       const RawDecodeSettings& settings,
                       LibRawOutputContract output_contract,
                       photo_editor::pipeline::DecodedImageMetadata* metadata) {
  if (metadata == nullptr) {
    return;
  }
  auto populated = photo_editor::pipeline::DecodedImageMetadata{};
  populated.valid = true;
  populated.is_raw = true;
  const auto camera_linear =
      output_contract == LibRawOutputContract::CameraLinear;
  populated.pixel_encoding =
      camera_linear
          ? photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb
          : photo_editor::pipeline::DecodedPixelEncoding::SceneLinearRgb;
  populated.decoder_backend =
      std::string(camera_linear ? "LibRaw camera-linear "
                                : "LibRaw characterized linear-sRGB ") +
      libraw_version();
  populated.decoded_color_space_name =
      camera_linear
          ? "camera RGB after black subtraction, demosaic, and range "
            "normalization; white balance and characterization are engine-owned"
          : "Linear sRGB";
  populated.camera_profile_identity =
      std::string(camera_linear ? "uncharacterized-camera-rgb:"
                                : "libraw-camera-characterization:") +
      raw.idata.make + ":" + raw.idata.model;
  populated.black_subtraction_applied = true;
  populated.white_normalization_applied = true;
  populated.demosaic_applied = true;
  populated.white_balance_applied = !camera_linear;
  // LibRaw mode 1 preserves channel headroom but does not own the engine's
  // opposed-channel highlight reconstruction. Lens correction is likewise
  // deferred, and DNG baseline exposure remains an engine recipe fact.
  populated.highlight_reconstruction_applied = false;
  populated.lens_correction_applied = false;
  populated.baseline_exposure_applied = false;
  populated.camera_transform_applied = !camera_linear;
  populated.decoder_rendering_applied = std::abs(settings.exposure) > 1.0e-9 ||
                                        settings.highlight_recovery_enabled;
  populated.input_profile_name =
      photo_editor::pipeline::normalize_input_color_profile_name(
          settings.input_color_profile.toStdString());
  populated.camera_make = raw.idata.make;
  populated.camera_model = raw.idata.model;
  const auto as_shot_white_balance = estimate_as_shot_white_balance(raw.color);
  populated.as_shot_temperature = settings.white_balance_override_enabled
                                      ? std::clamp(
                                            settings.white_balance_temperature,
                                            2000.0,
                                            12000.0)
                                      : as_shot_white_balance.first;
  populated.as_shot_tint = settings.white_balance_override_enabled
                                ? std::clamp(
                                      settings.white_balance_tint,
                                      -150.0,
                                      150.0)
                                : as_shot_white_balance.second;
  if (camera_linear) {
    const auto characterization = resolve_decoder_camera_characterization(
        raw.color,
        populated.as_shot_temperature,
        populated.as_shot_tint,
        settings.white_balance_override_enabled);
    populated.decoder_camera_to_xyz_valid = characterization.valid;
    populated.decoder_camera_to_xyz = characterization.camera_to_xyz;
    populated.decoder_camera_to_xyz_reference_white =
        characterization.reference_white;
    if (characterization.valid) {
      populated.camera_profile_identity =
          std::string("libraw-camera-to-xyz-") +
          characterization.reference_white + ":" + raw.idata.make + ":" +
          raw.idata.model;
    }
  }
  populated.baseline_exposure = sanitized_baseline_exposure(
      raw.color.dng_levels.baseline_exposure);
  populated.highlight_headroom = 1.0;
  *metadata = std::move(populated);
}

[[nodiscard]] bool configure_processing(libraw_data_t* raw, int max_edge,
                                        const RawDecodeSettings& settings,
                                        LibRawOutputContract output_contract) {
  auto& params = raw->params;
  const auto long_edge = std::max(static_cast<int>(raw->sizes.width),
                                  static_cast<int>(raw->sizes.height));
  params.half_size = max_edge > 0 && long_edge > max_edge * 2 ? 1 : 0;
  params.output_bps = 16;
  const auto camera_linear =
      output_contract == LibRawOutputContract::CameraLinear;
  params.output_color = camera_linear ? 0 : 1;
  params.use_camera_matrix = camera_linear ? 0 : 1;
  params.no_auto_bright = 1;
  params.no_auto_scale = 0;
  params.gamm[0] = 1.0;
  params.gamm[1] = 1.0;
  params.user_qual =
      uses_draft_demosaic(settings, params.half_size != 0) ? 0 : 3;
  params.highlight = settings.highlight_recovery_enabled ? 1 : 0;
  params.exp_correc = std::abs(settings.exposure) > 1.0e-9 ? 1 : 0;
  params.exp_shift =
      static_cast<float>(std::exp2(std::clamp(settings.exposure, -5.0, 5.0)));
  params.exp_preser = 0.0F;
  params.use_auto_wb = 0;
  params.use_camera_wb = camera_linear ? 0 : 1;
  if (camera_linear) {
    // DCP camera-to-PCS transforms consume the original camera-channel
    // ratios. Applying LibRaw white balance here and the source-white-aware
    // DCP transform later applies the illuminant correction twice, producing
    // severe casts on some DNGs. Unity multipliers make the engine the sole
    // white-balance and camera-characterization owner.
    std::fill(std::begin(params.user_mul), std::end(params.user_mul), 1.0F);
  } else if (settings.white_balance_override_enabled) {
    if (!interpolate_white_balance(
            raw->color,
            std::clamp(settings.white_balance_temperature, 2000.0, 12000.0),
            settings.white_balance_tint, params.user_mul)) {
      return false;
    }
    params.use_camera_wb = 0;
  }
  return true;
}

[[nodiscard]] QSize oriented_size(const libraw_image_sizes_t& sizes) {
  auto width = static_cast<int>(sizes.width);
  auto height = static_cast<int>(sizes.height);
  if (sizes.flip == 5 || sizes.flip == 6) {
    std::swap(width, height);
  }
  return width > 0 && height > 0 ? QSize(width, height) : QSize{};
}

#endif

}  // namespace

RawCameraLinearDecoderCapability raw_camera_linear_decoder_capability() {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  const auto compiled = normalized_version(LIBRAW_VERSION_STR);
  const auto runtime = normalized_version(libraw_version());
  const auto validated = std::string(PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION);
  const auto strict = compiled == validated && runtime == validated;
  return RawCameraLinearDecoderCapability{
      .available = true,
      .strict_version = strict,
      .backend = QStringLiteral("LibRaw"),
      .compiled_version = QString::fromStdString(compiled),
      .runtime_version = QString::fromStdString(runtime),
      .validated_version = QString::fromStdString(validated),
      .pixel_contract =
          QStringLiteral("camera-linear-rgb:black-subtracted:unbalanced:"
                         "demosaiced:normalized:no-camera-matrix"),
      .applied_controls = QStringLiteral(
          "demosaic-quality,exposure,highlight-recovery,hot-dead-pixel,"
          "false-color"),
      .deferred_controls = QStringLiteral(
          "white-balance,camera-characterization,gamut-mapping,"
          "lens-correction,boost,shadow-boost,detail,chroma-noise,moire"),
      .maximum_raw_memory_bytes = kMaximumRawMemoryBytes,
  };
#else
  return RawCameraLinearDecoderCapability{
      .backend = QStringLiteral("unavailable"),
      .validated_version =
          QStringLiteral(PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION),
      .pixel_contract = QStringLiteral("camera-linear-rgb"),
      .applied_controls = QStringLiteral("none"),
      .deferred_controls = QStringLiteral("all"),
      .maximum_raw_memory_bytes = kMaximumRawMemoryBytes,
      .failure_reason = QStringLiteral("libraw-camera-linear-not-compiled"),
  };
#endif
}

bool raw_decode_requests_camera_linear_backend(
    const RawDecodeSettings& settings) {
  // Every editable RAW uses one engine-owned camera-linear boundary. The
  // characterized decoder fallback exists only when the RAW input transform
  // has been explicitly disabled (and for the direct display helper).
  return settings.input_transform_enabled;
}

QString raw_decode_settings_cache_key(
    const RawDecodeSettings& settings) {
  return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
      .arg(settings.input_transform_enabled ? 1 : 0)
      .arg(settings.demosaic_quality.trimmed().toLower())
      .arg(settings.exposure, 0, 'f', 6)
      .arg(settings.hot_dead_pixel_cleanup_amount, 0, 'f', 6)
      .arg(settings.false_color_suppression_amount, 0, 'f', 6)
      .arg(settings.highlight_recovery_enabled ? 1 : 0)
      .arg(settings.white_balance_override_enabled ? 1 : 0)
      .arg(settings.white_balance_temperature, 0, 'f', 3)
      .arg(settings.white_balance_tint, 0, 'f', 3);
}

bool libraw_camera_linear_image_supported(const QString& file_path) {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  return open_raw(file_path) != nullptr;
#else
  (void)file_path;
  return false;
#endif
}

LibRawDecodeCacheStats libraw_decode_cache_stats() {
  auto& cache = raw_sample_cache();
  auto stats = LibRawDecodeCacheStats{};
  {
    QMutexLocker lock(&cache.mutex);
    stats = cache.stats;
    stats.entries = static_cast<int>(cache.entries.size());
  }
  auto& metadata_cache = raw_metadata_cache();
  {
    QMutexLocker lock(&metadata_cache.mutex);
    stats.metadata_hits = metadata_cache.hits;
    stats.metadata_misses = metadata_cache.misses;
    stats.metadata_entries = static_cast<int>(metadata_cache.entries.size());
  }
  return stats;
}

void clear_libraw_decode_cache() {
  auto& cache = raw_sample_cache();
  QMutexLocker lock(&cache.mutex);
  cache.entries.clear();
  cache.stats = {};
  lock.unlock();
  auto& metadata_cache = raw_metadata_cache();
  QMutexLocker metadata_lock(&metadata_cache.mutex);
  metadata_cache.entries.clear();
  metadata_cache.hits = 0;
  metadata_cache.misses = 0;
}

QSize libraw_camera_linear_image_size(const QString& file_path) {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  const auto raw = open_raw(file_path);
  return raw == nullptr ? QSize{} : oriented_size(raw->sizes);
#else
  (void)file_path;
  return {};
#endif
}

static bool decode_libraw_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings, LibRawOutputContract output_contract,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken cancellation) {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  if (image == nullptr || max_edge < 0 ||
      cancellation.cancelled()) {
    return false;
  }
  auto raw = open_raw(file_path);
  if (raw == nullptr || cancellation.cancelled()) {
    return false;
  }
  libraw_set_progress_handler(
      raw.get(),
      cancellation_progress_callback,
      &cancellation);
  if (!configure_processing(raw.get(), max_edge, settings, output_contract)) return false;
  const QFileInfo info(file_path);
  const auto key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
      .arg(info.canonicalFilePath()).arg(info.size())
      .arg(info.lastModified().toMSecsSinceEpoch())
      .arg(info.metadataChangeTime().toMSecsSinceEpoch())
      .arg(raw_decode_settings_cache_key(settings))
      .arg(static_cast<int>(output_contract)).arg(raw->params.half_size);
  auto samples = DecodedRawSamples{};
  auto uncached_processed = LibRawImage{};
  auto& cache = raw_sample_cache();
  {
    QMutexLocker lock(&cache.mutex);
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
      if (it->key != key) continue;
      samples = *it;
      cache.entries.erase(it);
      cache.entries.push_front(samples);
      ++cache.stats.hits;
      break;
    }
    if (samples.pixels.isEmpty()) ++cache.stats.misses;
  }
  if (samples.pixels.isEmpty()) {
    auto staged_metadata = photo_editor::pipeline::DecodedImageMetadata{};
    if (libraw_unpack(raw.get()) != LIBRAW_SUCCESS ||
        cancellation.cancelled() ||
        libraw_dcraw_process(raw.get()) != LIBRAW_SUCCESS ||
        cancellation.cancelled()) {
      return false;
    }
    populate_metadata(*raw, settings, output_contract, &staged_metadata);
    auto result_code = int{LIBRAW_SUCCESS};
    auto processed =
        LibRawImage(libraw_dcraw_make_mem_image(raw.get(), &result_code));
    if (processed == nullptr || result_code != LIBRAW_SUCCESS ||
        processed->type != LIBRAW_IMAGE_BITMAP || processed->bits != 16U ||
        processed->colors != 3U || processed->width == 0U ||
        processed->height == 0U) {
      return false;
    }
    const auto pixel_count = static_cast<std::uint64_t>(processed->width) *
                             static_cast<std::uint64_t>(processed->height);
    constexpr auto kChannels = std::uint64_t{3U};
    if (pixel_count == 0U || pixel_count > kMaximumDecodedPixels ||
        pixel_count > std::numeric_limits<std::uint64_t>::max() / kChannels /
                          sizeof(std::uint16_t) ||
        processed->data_size != pixel_count * kChannels * sizeof(std::uint16_t)) {
      return false;
    }
    if (cancellation.cancelled()) return false;
    samples = DecodedRawSamples{key, {}, static_cast<int>(processed->width),
        static_cast<int>(processed->height), std::move(staged_metadata)};
    if (processed->data_size <= kRawSampleCacheBudget && !cancellation.cancelled()) {
      samples.pixels = QByteArray(reinterpret_cast<const char*>(processed->data), processed->data_size);
      QMutexLocker lock(&cache.mutex);
      for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
        if (it->key == key) {
          cache.stats.bytes -= it->pixels.size();
          cache.entries.erase(it);
          break;
        }
      }
      while (!cache.entries.empty() && (cache.entries.size() >= 2 ||
          cache.stats.bytes + samples.pixels.size() > kRawSampleCacheBudget)) {
        cache.stats.bytes -= cache.entries.back().pixels.size();
        cache.entries.pop_back();
      }
      cache.entries.push_front(samples);
      cache.stats.bytes += samples.pixels.size();
    } else {
      uncached_processed = std::move(processed);
    }
  }
  // Release decoder buffers before allocating float pixels, on both paths.
  raw.reset();
  if (cancellation.cancelled()) return false;
  auto staged = QImage(samples.width, samples.height, QImage::Format_RGBA32FPx4);
  if (staged.isNull()) {
    return false;
  }
  const auto* source = reinterpret_cast<const std::uint16_t*>(uncached_processed ?
      uncached_processed->data : reinterpret_cast<const unsigned char*>(samples.pixels.constData()));
  constexpr auto kScale = 1.0F / 65535.0F;
  for (int y = 0; y < staged.height(); ++y) {
    if (cancellation.cancelled()) {
      return false;
    }
    auto* destination = reinterpret_cast<QRgbaFloat32*>(staged.scanLine(y));
    const auto row_offset = static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(staged.width()) * 3U;
    for (int x = 0; x < staged.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * 3U;
      destination[x] = QRgbaFloat32{
          static_cast<float>(source[offset]) * kScale,
          static_cast<float>(source[offset + 1U]) * kScale,
          static_cast<float>(source[offset + 2U]) * kScale,
          1.0F,
      };
    }
  }
  if (max_edge > 0 && std::max(staged.width(), staged.height()) > max_edge) {
    if (cancellation.cancelled()) {
      return false;
    }
    staged = staged.scaled(max_edge, max_edge, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    if (staged.isNull() ||
        cancellation.cancelled()) {
      return false;
    }
  }
  if (output_contract == LibRawOutputContract::CharacterizedLinearSrgb) {
    staged.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  }
  *image = std::move(staged);
  if (metadata != nullptr) {
    *metadata = std::move(samples.metadata);
  }
  return true;
#else
  (void)file_path;
  (void)image;
  (void)max_edge;
  (void)settings;
  (void)output_contract;
  (void)metadata;
  (void)cancellation;
  return false;
#endif
}

bool decode_libraw_camera_linear_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken cancellation) {
  return decode_libraw_image(file_path, image, max_edge, settings,
                             LibRawOutputContract::CameraLinear, metadata,
                             cancellation);
}

bool decode_libraw_scene_linear_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken cancellation) {
  return decode_libraw_image(file_path, image, max_edge, settings,
                             LibRawOutputContract::CharacterizedLinearSrgb,
                             metadata, cancellation);
}

static bool read_libraw_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings, LibRawOutputContract output_contract) {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && \
    PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  if (metadata == nullptr) {
    return false;
  }
  auto key = QString{};
  if (!raw_metadata_cache_key(
          file_path, settings, static_cast<int>(output_contract), &key)) {
    return false;
  }
  auto& cache = raw_metadata_cache();
  {
    QMutexLocker lock(&cache.mutex);
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
      if (it->key != key) {
        continue;
      }
      auto entry = *it;
      cache.entries.erase(it);
      cache.entries.push_front(std::move(entry));
      *metadata = cache.entries.front().metadata;
      ++cache.hits;
      return true;
    }
    ++cache.misses;
  }
  const auto raw = open_raw(file_path);
  if (raw == nullptr) {
    return false;
  }
  auto staged = photo_editor::pipeline::DecodedImageMetadata{};
  populate_metadata(*raw, settings, output_contract, &staged);
  if (!staged.valid) {
    return false;
  }
  {
    QMutexLocker lock(&cache.mutex);
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
      if (it->key == key) {
        cache.entries.erase(it);
        break;
      }
    }
    cache.entries.push_front(RawMetadataCacheEntry{key, staged});
    while (cache.entries.size() > kRawMetadataCacheCapacity) {
      cache.entries.pop_back();
    }
  }
  *metadata = std::move(staged);
  return true;
#else
  (void)file_path;
  (void)metadata;
  (void)settings;
  (void)output_contract;
  return false;
#endif
}

QVariantMap read_libraw_capture_camera_metadata(const QString& file_path) {
#if defined(PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR) && PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR
  const auto raw = open_raw(file_path);
  if (!raw) return {{"available", false}, {"backend", "LibRaw"}};
  const auto text = [](const auto& value) {
    return QString::fromUtf8(value, static_cast<qsizetype>(sizeof(value))).section(QChar(0), 0, 0).trimmed();
  };
  const auto number = [](double value) { return std::isfinite(value) && value > 0 && value <= 10000 ? value : 0.0; };
  return {{"available", true}, {"backend", "LibRaw"}, {"make", text(raw->idata.make)},
      {"model", text(raw->idata.model)}, {"lens", text(raw->lens.Lens)},
      {"bodySerial", text(raw->shootinginfo.BodySerial)}, {"focalMm", number(raw->other.focal_len)},
      {"focal35Mm", number(raw->lens.FocalLengthIn35mmFormat)}};
#else
  (void)file_path;
  return {{"available", false}, {"backend", "unavailable"}};
#endif
}

bool read_libraw_camera_linear_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings) {
  return read_libraw_metadata(file_path, metadata, settings,
                              LibRawOutputContract::CameraLinear);
}

bool read_libraw_scene_linear_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings) {
  return read_libraw_metadata(file_path, metadata, settings,
                              LibRawOutputContract::CharacterizedLinearSrgb);
}

}  // namespace photo_editor::desktop
