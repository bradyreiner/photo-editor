#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <QString>

#include <array>
#include <chrono>
#include <cstddef>

namespace photo_editor::desktop {

enum class PhotoRenderProfileLayerKind : std::size_t {
  text,
  source,
  vector,
  pixel,
  fill,
  group,
  adjustment,
  other,
  count,
};

struct PhotoRenderProfileForTests final {
  bool enabled = false;
  qint64 compile_export_pipeline_state_ns = 0;
  qint64 apply_color_adjustments_ns = 0;
  qint64 expand_smart_documents_ns = 0;
  qint64 expand_smart_documents_calls = 0;
  qint64 expanded_smart_document_layers = 0;
  qint64 expanded_smart_document_masks = 0;
  qint64 expansion_for_photo_layer_compile_calls = 0;
  qint64 expansion_for_display_text_calls = 0;
  qint64 compile_pipeline_photo_layers_ns = 0;
  qint64 compile_pipeline_photo_layers_calls = 0;
  qint64 compiled_photo_layer_count = 0;
  std::array<qint64, static_cast<std::size_t>(PhotoRenderProfileLayerKind::count)>
      populate_photo_layer_pixels_ns_by_kind{};
  std::array<qint64, static_cast<std::size_t>(PhotoRenderProfileLayerKind::count)>
      populate_photo_layer_pixels_calls_by_kind{};
};

inline thread_local PhotoRenderProfileForTests photo_render_profile_for_tests;

[[nodiscard]] inline PhotoRenderProfileLayerKind photo_render_profile_layer_kind(
    const QString& kind) {
  if (kind == QStringLiteral("text")) return PhotoRenderProfileLayerKind::text;
  if (kind == QStringLiteral("source")) return PhotoRenderProfileLayerKind::source;
  if (kind == QStringLiteral("vector")) return PhotoRenderProfileLayerKind::vector;
  if (kind == QStringLiteral("pixel")) return PhotoRenderProfileLayerKind::pixel;
  if (kind == QStringLiteral("fill")) return PhotoRenderProfileLayerKind::fill;
  if (kind == QStringLiteral("group")) return PhotoRenderProfileLayerKind::group;
  if (kind == QStringLiteral("adjustment")) return PhotoRenderProfileLayerKind::adjustment;
  return PhotoRenderProfileLayerKind::other;
}

class PhotoRenderProfileTimerForTests final {
 public:
  explicit PhotoRenderProfileTimerForTests(qint64* elapsed_ns)
      : elapsed_ns_(photo_render_profile_for_tests.enabled ? elapsed_ns : nullptr),
        started_(elapsed_ns_ == nullptr ? Clock::time_point{} : Clock::now()) {}

  ~PhotoRenderProfileTimerForTests() {
    if (elapsed_ns_ != nullptr) {
      *elapsed_ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
                          Clock::now() - started_)
                          .count();
    }
  }

  PhotoRenderProfileTimerForTests(const PhotoRenderProfileTimerForTests&) = delete;
  PhotoRenderProfileTimerForTests& operator=(const PhotoRenderProfileTimerForTests&) = delete;

 private:
  using Clock = std::chrono::steady_clock;
  qint64* elapsed_ns_ = nullptr;
  Clock::time_point started_{};
};

}  // namespace photo_editor::desktop
