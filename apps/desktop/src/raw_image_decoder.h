#pragma once

#include "photo_editor/pipeline/color_pipeline.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace photo_editor::desktop {

struct RawDecodeSettings final {
  bool input_transform_enabled = true;
  QString input_color_profile = QStringLiteral("Embedded");
  QString highlight_reconstruction_mode = QStringLiteral("Enabled");
  QString chromatic_adaptation_method = QStringLiteral("Bradford");
  QString demosaic_quality = QStringLiteral("Balanced");
  double exposure = 0.0;
  double boost_amount = 0.0;
  double boost_shadow_amount = 1.0;
  double detail_amount = 0.0;
  double chroma_noise_reduction_amount = 0.0;
  double moire_reduction_amount = 0.0;
  double hot_dead_pixel_cleanup_amount = 0.0;
  double false_color_suppression_amount = 0.0;
  bool highlight_recovery_enabled = true;
  bool gamut_mapping_enabled = true;
  bool lens_correction_enabled = true;
  bool white_balance_override_enabled = false;
  double white_balance_temperature = 5200.0;
  double white_balance_tint = 0.0;
};

[[nodiscard]] bool raw_decode_uses_draft_demosaic(
    const RawDecodeSettings& settings,
    bool scaled_preview);
[[nodiscard]] double raw_decode_chroma_noise_reduction_amount(const RawDecodeSettings& settings);
[[nodiscard]] double raw_decode_moire_reduction_amount(const RawDecodeSettings& settings);
[[nodiscard]] double raw_decode_hot_dead_pixel_cleanup_amount(const RawDecodeSettings& settings);
[[nodiscard]] double raw_decode_false_color_suppression_amount(const RawDecodeSettings& settings);
bool apply_raw_hot_dead_pixel_cleanup(QImage* image, double amount);
bool apply_raw_false_color_suppression(QImage* image, double amount);
[[nodiscard]] bool raw_image_supported(const QString& file_path);
[[nodiscard]] QSize raw_image_size(const QString& file_path);
[[nodiscard]] bool decode_raw_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RawDecodeSettings& settings = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr);
[[nodiscard]] bool decode_raw_preview_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0);
[[nodiscard]] bool decode_raw_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RawDecodeSettings& settings = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr);
[[nodiscard]] bool read_raw_decoded_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings = {});
[[nodiscard]] bool native_image_supported(const QString& file_path);
[[nodiscard]] QSize native_image_size(const QString& file_path);
[[nodiscard]] bool decode_native_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0);
[[nodiscard]] bool decode_native_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0);

}  // namespace photo_editor::desktop
