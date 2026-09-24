#pragma once
#include <QString>
#include <QSize>
#include <QtGlobal>
#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "source_image_decode_cache.h"

class QImage;
namespace photo_editor::desktop {

struct SourceDecodeTelemetry final {
  bool cache_lookup = false;
  bool cache_hit = false;
  QString backend;
  int cache_entries = 0;
  qint64 cache_bytes = 0;
};
[[nodiscard]] bool source_image_supported(const QString& file_path);
[[nodiscard]] QSize source_image_size(const QString& file_path);
[[nodiscard]] bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const EditRecipeState& state = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {},
    SourceDecodeTelemetry* telemetry = nullptr);
}  // namespace photo_editor::desktop
