#pragma once

#include "timeline_native_generator_frame_cache.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace photo_editor::desktop::timeline_motion::bounded_h264 {

inline constexpr double kMaximumRenderWindowSeconds = 4.0;
inline constexpr double kCacheBudgetFraction = 0.75;
inline constexpr int kBoundedFilterComplexThreads = 1;
inline constexpr int kBoundedImageDecoderThreads = 1;
inline constexpr int kBoundedH264EncoderThreads = 2;
inline constexpr qsizetype kMaximumFilterGraphInputCount = 256;

struct FrameRange final {
  qint64 start_frame = 0;
  qint64 end_frame = 0;
  qint64 live_procedural_source_count = 0;
  qint64 estimated_mask_source_bytes = 0;
  qint64 estimated_source_bytes = 0;
  bool native_mask_cache_enabled = true;
  QString mask_cache_fallback_reason;
};

using LiveSourceCounter = std::function<qint64(qint64 start_frame, qint64 end_frame)>;
using AdditionalSourceByteCounter =
    std::function<qint64(qint64 start_frame, qint64 end_frame)>;

// Constrain the memory-heavy parts of one bounded H.264 FFmpeg invocation
// without changing input order, filters, frame clocks, or output quality.
// Only still-image inputs receive decoder limits; video sources keep their
// existing codec behavior.
[[nodiscard]] QStringList apply_ffmpeg_thread_limits(
    const QStringList& arguments,
    QString* error_message = nullptr);

[[nodiscard]] QStringList build_final_video_audio_mux_arguments(
    const QString& concat_path,
    const QString& audio_path,
    const QString& output_path,
    qint64 total_frames,
    int audio_sample_rate,
    const QString& audio_bitrate,
    QString* error_message = nullptr);

[[nodiscard]] QVector<FrameRange> plan_frame_ranges(
    qint64 total_frames,
    double frames_per_second,
    const QSize& frame_size,
    const LiveSourceCounter& live_source_counter,
    QString* error_message = nullptr,
    qint64 cache_budget_bytes = kNativeGeneratorFrameCacheBudgetBytes,
    const AdditionalSourceByteCounter& additional_source_byte_counter = {});

}  // namespace photo_editor::desktop::timeline_motion::bounded_h264
