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
// Source-aware brush analysis uses characterized, unclipped linear sRGB rather
// than interpreting untagged camera samples or wide-gamut bytes as display RGB.
[[nodiscard]] QImage prepare_brush_color_source_image(
    const QImage& decoded, const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] bool decode_brush_color_source_image(
    const QString& file_path, QImage* image, int max_edge,
    const EditRecipeState& state,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const EditRecipeState& state = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {},
    SourceDecodeTelemetry* telemetry = nullptr);
}  // namespace photo_editor::desktop
