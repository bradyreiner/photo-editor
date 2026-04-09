#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace photo_editor::desktop {

struct RawDecodeSettings final {
  double exposure = 0.0;
  double boost_amount = 0.0;
  double boost_shadow_amount = 1.0;
  double detail_amount = 0.0;
  bool highlight_recovery_enabled = true;
  bool gamut_mapping_enabled = true;
  bool lens_correction_enabled = true;
  bool white_balance_override_enabled = false;
  double white_balance_temperature = 5200.0;
  double white_balance_tint = 0.0;
};

[[nodiscard]] bool raw_image_supported(const QString& file_path);
[[nodiscard]] QSize raw_image_size(const QString& file_path);
[[nodiscard]] bool decode_raw_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RawDecodeSettings& settings = {});
[[nodiscard]] bool decode_raw_linear_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
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
