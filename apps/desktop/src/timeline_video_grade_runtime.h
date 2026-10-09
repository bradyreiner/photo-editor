#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <QColorSpace>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace photo_editor::desktop {

struct EditRecipeState;

struct TimelineVideoGradeLut final {
  std::shared_ptr<const photo_editor::pipeline::ColorLut3D> lut;
  QString file_path;
  QString error;
  int size = 0;
  int validation_sample_count = 0;
  double validation_p99_absolute_error = 0.0;
  double validation_maximum_absolute_error = 0.0;

  [[nodiscard]] bool ok() const {
    return lut != nullptr && error.isEmpty();
  }
};

struct TimelineVideoGradeState final {
  std::shared_ptr<const EditRecipeState> recipe;
  std::shared_ptr<const photo_editor::pipeline::ColorAdjustmentState> state;
  std::shared_ptr<const photo_editor::pipeline::RenderPlan> render_plan;
  QString error;

  [[nodiscard]] bool ok() const {
    return recipe != nullptr && state != nullptr && render_plan != nullptr &&
           error.isEmpty();
  }
};

struct TimelineVideoGradeRenderSessionData;

// A serial render session keeps the expensive recipe, masks, and compiled
// frame plan resident while supported scalar controls move over time.
struct TimelineVideoGradeRenderSession final {
  std::shared_ptr<TimelineVideoGradeRenderSessionData> data;
  QString error;

  [[nodiscard]] bool ok() const {
    return data != nullptr && error.isEmpty();
  }
};

struct TimelineVideoGradeFrame final {
  QImage image;
  QString error;

  [[nodiscard]] bool ok() const {
    return !image.isNull() && error.isEmpty();
  }
};

[[nodiscard]] const QColorSpace& timeline_video_rec709_color_space();

// Playback renders the photo recipe's complete graph at the visible frame
// dimensions. CUBE compilation is available only for pointwise recipes; the
// current sequence exporter consumes that LUT form.
[[nodiscard]] QString normalize_timeline_video_grade_recipe(
    const QString& recipe_json,
    QString* normalized_recipe_json);

[[nodiscard]] TimelineVideoGradeState compile_timeline_video_grade_state(
    const QString& normalized_recipe_json,
    const QSize& output_size);

[[nodiscard]] TimelineVideoGradeFrame render_timeline_video_grade_frame(
    const QImage& rec709_frame,
    const QString& normalized_recipe_json,
    const QString& stable_clip_id,
    const QColorSpace& target_color_space,
    photo_editor::pipeline::CancellationToken cancellation = {});

[[nodiscard]] TimelineVideoGradeRenderSession
create_timeline_video_grade_render_session(
    const QString& normalized_recipe_json,
    const QString& stable_clip_id,
    const QSize& output_size);

[[nodiscard]] TimelineVideoGradeFrame render_timeline_video_grade_frame(
    const QImage& rec709_frame,
    TimelineVideoGradeRenderSession& session,
    const QVariantMap& animated_parameters,
    const QColorSpace& target_color_space,
    photo_editor::pipeline::CancellationToken cancellation = {});

[[nodiscard]] bool timeline_video_grade_can_use_lut(
    const QString& normalized_recipe_json,
    const QSize& output_size,
    QString* error = nullptr);

[[nodiscard]] TimelineVideoGradeLut compile_timeline_video_grade_lut(
    const QString& normalized_recipe_json,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] QVariantMap timeline_video_grade_cache_metrics();

}  // namespace photo_editor::desktop
