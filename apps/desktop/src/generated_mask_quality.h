#pragma once

#include <QVariantMap>

namespace photo_editor::desktop {

struct GeneratedMaskRasterStats final {
  bool valid = false;
  bool has_confidence = false;
  bool touches_edge = false;
  int selected_pixels = 0;
  double coverage = 0.0;
  double mean_confidence = 1.0;
};

[[nodiscard]] GeneratedMaskRasterStats generated_mask_raster_stats(
    const QVariantMap& mask);

[[nodiscard]] QVariantMap generated_mask_quality_report_for_mask(
    const QVariantMap& mask,
    bool stale);

}  // namespace photo_editor::desktop
