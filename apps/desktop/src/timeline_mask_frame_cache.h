#pragma once

#include "timeline_native_generator_frame_cache.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

namespace photo_editor::desktop::timeline_motion {

struct NativeMaskFrameSample final {
  double sequence_seconds = 0.0;
  double left = 0.0;
  double top = 0.0;
  double right = 1.0;
  double bottom = 1.0;
  double corner_radius = 0.0;
};

struct NativeMaskFrameSequenceRequest final {
  QString cache_root;
  // Informational owner used by the timeline render graph. This does not
  // affect cache identity because identical masks may share one decoded input.
  QString owner_clip_id;
  // "frame-corner" is applied in the frame-mode source dimensions before
  // animated frame scaling. "sequence" is applied in output-canvas space.
  QString purpose;
  QString shape;
  QSize frame_size;
  double frames_per_second = 30.0;
  bool high_precision = false;
  QVector<NativeMaskFrameSample> samples;
};

enum class NativeMaskFrameSequenceFailure {
  None,
  InvalidRequest,
  Unsupported,
  Cancelled,
  CacheCapacity,
  CorruptCache,
  Io,
};

struct NativeMaskFrameSequenceResult final {
  bool ok = false;
  bool cache_hit = false;
  bool lease_acquired = false;
  NativeMaskFrameSequenceFailure failure = NativeMaskFrameSequenceFailure::None;
  QString cache_key;
  QString frame_directory;
  QString frame_pattern;
  QString error;
  qint64 encoded_bytes = 0;
  qint64 decoded_bytes = 0;
  qint64 elapsed_milliseconds = 0;
  int frame_count = 0;
};

inline constexpr qint64 kNativeMaskFrameCacheBudgetBytes =
    2LL * 1024LL * 1024LL * 1024LL;

[[nodiscard]] QString native_mask_frame_sequence_cache_key(
    const NativeMaskFrameSequenceRequest& request);
// Estimate FFmpeg's decoded input storage (RGBA is 4 bytes/pixel for SDR and
// 8 bytes/pixel for the 16-bit path), not the much smaller PNG file size.
[[nodiscard]] qint64 native_mask_frame_sequence_decoded_bytes(
    const NativeMaskFrameSequenceRequest& request);
// Produces straight white RGBA with mask coverage in alpha so a single native
// blend operation can preserve all source RGB and multiply source alpha.
[[nodiscard]] QImage render_native_mask_frame(
    const NativeMaskFrameSequenceRequest& request,
    const NativeMaskFrameSample& sample,
    QString* error_message = nullptr);
[[nodiscard]] QString native_mask_frame_blend_filter(
    const QString& source_label,
    const QString& coverage_label,
    const QString& output_label,
    bool high_precision,
    QString* error_message = nullptr);
[[nodiscard]] NativeMaskFrameSequenceResult ensure_native_mask_frame_sequence(
    const NativeMaskFrameSequenceRequest& request,
    const std::atomic_bool* cancelled = nullptr,
    const std::function<bool()>& cancellation_check = {},
    bool pin_result = true);

}  // namespace photo_editor::desktop::timeline_motion
