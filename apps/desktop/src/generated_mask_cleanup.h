#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

inline constexpr double kDefaultGeneratedMaskHoleFillThreshold = 0.004;
inline constexpr double kDefaultGeneratedMaskIslandRemovalThreshold = 0.0015;
inline constexpr double kMaxGeneratedMaskCleanupThreshold = 0.05;

struct GeneratedMaskCleanupResult final {
  QByteArray data;
  int holes_filled = 0;
  int islands_removed = 0;
  int pixels_filled = 0;
  int pixels_removed = 0;
};

[[nodiscard]] int generated_mask_cleanup_component_limit(
    int width,
    int height,
    double threshold);

[[nodiscard]] GeneratedMaskCleanupResult cleanup_generated_mask_components(
    const QByteArray& source_data,
    int width,
    int height,
    double hole_threshold,
    double island_threshold);

[[nodiscard]] bool apply_generated_mask_cleanup(QVariantMap* mask);

[[nodiscard]] QString generated_mask_cleanup_summary_for_mask(
    const QVariantMap& mask);

}  // namespace photo_editor::desktop
