#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace photo_editor::desktop {

inline constexpr qint64 kMaxScaledMaskMatteCacheMemoryBytes = 32 * 1024 * 1024;
inline constexpr int kMaxScaledMaskMatteCacheEntries = 512;

struct ScaledMaskMatteCacheSnapshot final {
  // Scaled-resample counters/bytes remain separate for existing callers.
  // total_* account for both strategies under the shared cache budget.
  int entries = 0;
  qint64 bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;
  int brush_replay_entries = 0;
  qint64 brush_replay_bytes = 0;
  int brush_replay_hit_count = 0;
  int brush_replay_miss_count = 0;
  int brush_replay_eviction_count = 0;
  qint64 total_bytes = 0;
  int total_entries = 0;
};

[[nodiscard]] QVariantMap mask_with_scaled_segmentation_matte(
    const QVariantMap& mask,
    const QSize& target_size,
    const QString& purpose);

[[nodiscard]] QVariantMap scaled_mask_matte_metadata(const QVariantMap& mask);

void clear_scaled_mask_matte_cache();
[[nodiscard]] ScaledMaskMatteCacheSnapshot scaled_mask_matte_cache_snapshot();

}  // namespace photo_editor::desktop
