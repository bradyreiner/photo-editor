#include "editor_preview_controller.h"

#include "observability.h"
#include "worker_queue_registry.h"

namespace photo_editor::desktop {

namespace {

constexpr int kImmediateHistogramPreviewEdge = 192;

}  // namespace

void EditorPreviewController::computeHistogram(const QImage& image) {
  applyHistogramResult(compute_histogram_scope_data(image));
}

void EditorPreviewController::applyHistogramResult(const HistogramComputationResult& result) {
  histogram_scope_session_.publish(result);
  emit histogramChanged();
}

void EditorPreviewController::setScopesVisible(bool visible) {
  if (!histogram_scope_session_.setVisible(visible)) {
    return;
  }
  emit controlsChanged();
}

void EditorPreviewController::setScopeDomain(const QString& domain) {
  if (!histogram_scope_session_.setDomain(domain, soft_proof_enabled_)) {
    return;
  }
  emit controlsChanged();
  if (hasAsset()) {
    submitEditIntent(EditIntent{
        .operation = QStringLiteral("scope_domain"),
        .dirty_domain = QStringLiteral("histogram_scope"),
        .render_reason = QStringLiteral("scope_domain_change"),
        .schedule_full_resolution = false,
    });
  }
}

void EditorPreviewController::publishImmediateHistogramPreview(
    const QImage& image,
    int generation,
    bool full_resolution,
    int raw_clip_sample_count,
    int raw_shadow_clip_count,
    int raw_highlight_clip_count,
    const QString& render_reason,
    const QString& interaction_mode) {
  if (image.isNull() || !preview_session_.isCurrent(generation)) {
    return;
  }
  const auto fast_image =
      image.width() > kImmediateHistogramPreviewEdge || image.height() > kImmediateHistogramPreviewEdge
          ? image.scaled(
                kImmediateHistogramPreviewEdge,
                kImmediateHistogramPreviewEdge,
                Qt::KeepAspectRatio,
                Qt::FastTransformation)
          : image;
  auto result = compute_histogram_scope_data(fast_image);
  result.generation = generation;
  result.full_resolution = full_resolution;
  result.raw_clip_sample_count = raw_clip_sample_count;
  result.raw_shadow_clip_count = raw_shadow_clip_count;
  result.raw_highlight_clip_count = raw_highlight_clip_count;
  result.render_reason = render_reason;
  result.interaction_mode = interaction_mode;
  applyHistogramResult(result);
}

void EditorPreviewController::startHistogramUpdate(const RenderResult& result) {
  startHistogramUpdate(
      result.histogram_image,
      result.generation,
      result.full_resolution,
      result.raw_clip_sample_count,
      result.raw_shadow_clip_count,
      result.raw_highlight_clip_count,
      result.render_reason,
      result.interaction_mode);
}

void EditorPreviewController::startHistogramUpdate(
    const QImage& image,
    int generation,
    bool full_resolution,
    int raw_clip_sample_count,
    int raw_shadow_clip_count,
    int raw_highlight_clip_count,
    const QString& render_reason,
    const QString& interaction_mode) {
  if (image.isNull()) {
    observability::logWarning(
        photoEditorPerfLog(),
        QStringLiteral("preview.histogram.contained_failure"),
        {
            {QStringLiteral("failure_containment_contract"),
             QStringLiteral("preview-substage-failure-containment-v1")},
            {QStringLiteral("failure_stage"), QStringLiteral("histogram_scope")},
            {QStringLiteral("canvas_preview_failed"), false},
            {QStringLiteral("generation"), generation},
            {QStringLiteral("render_reason"), render_reason},
            {QStringLiteral("interaction_mode"), interaction_mode},
        });
    return;
  }

  publishImmediateHistogramPreview(
      image,
      generation,
      full_resolution,
      raw_clip_sample_count,
      raw_shadow_clip_count,
      raw_highlight_clip_count,
      render_reason,
      interaction_mode);

  const auto was_active = histogram_scope_coordinator_.active();
  const auto disposition = histogram_scope_coordinator_.submit(
      HistogramScopeRequest{
          .image = image,
          .generation = generation,
          .raw_clip_sample_count = raw_clip_sample_count,
          .raw_shadow_clip_count = raw_shadow_clip_count,
          .raw_highlight_clip_count = raw_highlight_clip_count,
          .full_resolution = full_resolution,
          .render_reason = render_reason,
          .interaction_mode = interaction_mode,
      });
  if (!was_active) {
    emit histogramActivityChanged();
  }
  if (disposition == HistogramScopeSubmitDisposition::Started) {
    WorkerQueueRegistry::logSample(
        QStringLiteral("histogram-scope"),
        QStringLiteral("histogram"),
        QStringLiteral("edit"),
        1,
        0,
        1,
        0);
  }
  updateProcessingState();
}

void EditorPreviewController::handleHistogramFinished() {
  const auto result = histogram_scope_coordinator_.result();
  const auto had_pending_request = histogram_scope_coordinator_.pending();
  WorkerQueueRegistry::logSummary(
      QStringLiteral("histogram-scope"),
      QStringLiteral("histogram"),
      QStringLiteral("edit"),
      0,
      had_pending_request ? 1 : 0,
      1,
      0,
      result.elapsed_ms,
      preview_session_.isCurrent(result.generation) ? 1 : 0,
      preview_session_.isCurrent(result.generation) ? 0 : 1);
  if (preview_session_.isCurrent(result.generation)) {
    applyHistogramResult(result);
    observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("preview.histogram.completed"),
        {
            {QStringLiteral("stage"), result.full_resolution ? QStringLiteral("full") : QStringLiteral("interactive")},
            {QStringLiteral("generation"), result.generation},
            {QStringLiteral("render_reason"), result.render_reason},
            {QStringLiteral("interaction_mode"), result.interaction_mode},
            {QStringLiteral("sample_count"), result.sample_count},
            {QStringLiteral("shadow_clip_count"), result.shadow_clip_count},
            {QStringLiteral("highlight_clip_count"), result.highlight_clip_count},
            {QStringLiteral("elapsed_ms"), result.elapsed_ms},
        });
  }

  if (histogram_scope_coordinator_.complete()) {
    WorkerQueueRegistry::logSample(
        QStringLiteral("histogram-scope"),
        QStringLiteral("histogram"),
        QStringLiteral("edit"),
        1,
        0,
        1,
        0);
    updateProcessingState();
    return;
  }

  emit histogramActivityChanged();
  updateProcessingState();
}

}  // namespace photo_editor::desktop
