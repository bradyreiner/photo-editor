#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace photo_editor::desktop {

struct RawDecodeSettings final {
  bool input_transform_enabled = true;
  QString input_color_profile = QStringLiteral("Embedded");
  QString demosaic_quality = QStringLiteral("Balanced");
  double exposure = 0.0;
  double hot_dead_pixel_cleanup_amount = 0.0;
  double false_color_suppression_amount = 0.0;
  bool highlight_recovery_enabled = true;
  bool white_balance_override_enabled = false;
  double white_balance_temperature = 5200.0;
  double white_balance_tint = 0.0;
};

struct RawCameraLinearDecoderCapability final {
  bool available = false;
  bool strict_version = false;
  QString backend;
  QString compiled_version;
  QString runtime_version;
  QString validated_version;
  QString pixel_contract;
  QString applied_controls;
  QString deferred_controls;
  quint64 maximum_raw_memory_bytes = 0U;
  QString failure_reason;
};

[[nodiscard]] RawCameraLinearDecoderCapability
raw_camera_linear_decoder_capability();
[[nodiscard]] bool raw_decode_requests_camera_linear_backend(
    const RawDecodeSettings& settings);
[[nodiscard]] QString raw_decode_settings_cache_key(
    const RawDecodeSettings& settings);

[[nodiscard]] bool raw_decode_uses_draft_demosaic(
    const RawDecodeSettings& settings,
    bool scaled_preview);
[[nodiscard]] double raw_decode_hot_dead_pixel_cleanup_amount(const RawDecodeSettings& settings);
[[nodiscard]] double raw_decode_false_color_suppression_amount(const RawDecodeSettings& settings);
bool apply_raw_hot_dead_pixel_cleanup(
    QImage* image,
    double amount,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
bool apply_raw_false_color_suppression(
    QImage* image,
    double amount,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool raw_image_supported(const QString& file_path);
[[nodiscard]] QSize raw_image_size(const QString& file_path);
[[nodiscard]] bool decode_raw_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RawDecodeSettings& settings = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool decode_raw_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RawDecodeSettings& settings = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool read_raw_decoded_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings = {});
[[nodiscard]] bool native_image_supported(const QString& file_path);
[[nodiscard]] QSize native_image_size(const QString& file_path);
[[nodiscard]] bool decode_native_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool decode_native_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    photo_editor::pipeline::CancellationToken
        cancellation = {});

}  // namespace photo_editor::desktop
