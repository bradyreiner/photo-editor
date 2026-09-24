#pragma once

#include "raw_image_decoder.h"

namespace photo_editor::desktop {

struct LibRawDecodeCacheStats {
  qint64 bytes = 0;
  int entries = 0;
  quint64 hits = 0;
  quint64 misses = 0;
};
[[nodiscard]] LibRawDecodeCacheStats libraw_decode_cache_stats();
void clear_libraw_decode_cache();

[[nodiscard]] bool libraw_camera_linear_image_supported(
    const QString& file_path);
[[nodiscard]] QSize libraw_camera_linear_image_size(const QString& file_path);
[[nodiscard]] bool decode_libraw_camera_linear_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool decode_libraw_scene_linear_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken
        cancellation = {});
[[nodiscard]] bool read_libraw_camera_linear_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings);
[[nodiscard]] bool read_libraw_scene_linear_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings);

}  // namespace photo_editor::desktop
