#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>

namespace photo_editor::desktop {

inline constexpr qint64 kMaxScaledMaskMatteCacheMemoryBytes = 32 * 1024 * 1024;

struct ScaledMaskMatteCacheSnapshot final {
  int entries = 0;
  qint64 bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;
};

[[nodiscard]] QVariantMap mask_with_scaled_segmentation_matte(
    const QVariantMap& mask,
    const QSize& target_size,
    const QString& purpose);

[[nodiscard]] QVariantMap scaled_mask_matte_metadata(const QVariantMap& mask);

void clear_scaled_mask_matte_cache();
[[nodiscard]] ScaledMaskMatteCacheSnapshot scaled_mask_matte_cache_snapshot();

}  // namespace photo_editor::desktop
