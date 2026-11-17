#include "editor_preview_controller.h"

#include "observability.h"
#include "worker_queue_registry.h"
#include <QDateTime>
#include <cmath>

namespace photo_editor::desktop {

namespace {

constexpr int kImmediateHistogramPreviewEdge = 192;

}  // namespace

void EditorPreviewController::computeHistogram(const QImage& image) {
  applyHistogramResult(compute_histogram_scope_data(image));
}

void EditorPreviewController::applyHistogramResult(const HistogramComputationResult& result) {
  histogram_scope_session_.publish(result);
  scope_published_at_msecs_ = QDateTime::currentMSecsSinceEpoch();
  scope_published_domain_ = scopeDomain();
  scope_published_region_ = scope_region_;
  emit histogramChanged();
}

void EditorPreviewController::setScopesVisible(bool visible) {
  if (!histogram_scope_session_.setVisible(visible)) {
    return;
  }
  emit controlsChanged();
  if (visible) refreshScopeSettings();
}

QVariantMap EditorPreviewController::scopeFrame() const {
  const auto& result = histogram_scope_session_.published();
  return {{QStringLiteral("red"), result.density_red},
          {QStringLiteral("green"), result.density_green},
          {QStringLiteral("blue"), result.density_blue},
          {QStringLiteral("luma"), result.density_luminance},
          {QStringLiteral("vectors"), result.vectorscope_points},
          {QStringLiteral("traceRed"), result.waveform_red},
          {QStringLiteral("traceGreen"), result.waveform_green},
          {QStringLiteral("traceBlue"), result.waveform_blue},
          {QStringLiteral("traceLuma"), result.waveform_luminance},
          {QStringLiteral("histogramRed"), result.red},
          {QStringLiteral("histogramGreen"), result.green},
          {QStringLiteral("histogramBlue"), result.blue},
          {QStringLiteral("histogramLuminance"), result.luminance},
          {QStringLiteral("shadowClipCount"), result.shadow_clip_count},
          {QStringLiteral("highlightClipCount"), result.highlight_clip_count},
          {QStringLiteral("sampleCount"), result.sample_count},
          {QStringLiteral("rejectedSamples"), result.rejected_sample_count},
          {QStringLiteral("columns"), result.columns},
          {QStringLiteral("levels"), result.levels},
          {QStringLiteral("minimum"), result.minimum},
          {QStringLiteral("maximum"), result.maximum},
          {QStringLiteral("domain"), scope_published_domain_},
          {QStringLiteral("region"), scope_published_region_},
          {QStringLiteral("generation"), result.generation},
          {QStringLiteral("publishedAt"), scope_published_at_msecs_},
          {QStringLiteral("computeMs"), result.elapsed_ms}};
}

void EditorPreviewController::refreshScopeSettings() {
  ++scope_configuration_;
  if (!hasAsset() || !scopesVisible() || scopes_frozen_) return;
  submitEditIntent(EditIntent{
      .operation = QStringLiteral("scope_settings"),
      .dirty_domain = QStringLiteral("histogram_scope"),
      .render_reason = QStringLiteral("scope_settings_change"),
      .schedule_full_resolution = false,
  });
}

void EditorPreviewController::setScopeSamplingMode(const QString& mode) {
  if ((mode != QStringLiteral("Fast") && mode != QStringLiteral("Balanced") &&
       mode != QStringLiteral("Precise")) || mode == scope_sampling_mode_) return;
  scope_sampling_mode_ = mode;
  emit controlsChanged();
  refreshScopeSettings();
}

void EditorPreviewController::setScopeRangeMode(const QString& mode) {
  if ((mode != QStringLiteral("SDR") && mode != QStringLiteral("Extended") &&
       mode != QStringLiteral("HDR")) || mode == scope_range_mode_) return;
  scope_range_mode_ = mode;
  emit controlsChanged();
  refreshScopeSettings();
}

void EditorPreviewController::setScopeRegion(const QRectF& region) {
  if (!std::isfinite(region.x()) || !std::isfinite(region.y()) ||
      !std::isfinite(region.width()) || !std::isfinite(region.height())) return;
  const auto bounded = region.normalized().intersected(QRectF(0, 0, 1, 1));
  if (bounded.isEmpty() || bounded == scope_region_) return;
  scope_region_ = bounded;
  emit controlsChanged();
  refreshScopeSettings();
}

void EditorPreviewController::setScopesFrozen(bool frozen) {
  if (frozen == scopes_frozen_) return;
  scopes_frozen_ = frozen;
  emit controlsChanged();
  refreshScopeSettings();
}

void EditorPreviewController::setScopeDomain(const QString& domain) {
  if (!histogram_scope_session_.setDomain(domain, soft_proof_enabled_)) {
    return;
  }
  ++scope_configuration_;
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

  if (scopes_frozen_) return;
  auto options = histogram_scope_options(scope_sampling_mode_,
      interaction_mode == QStringLiteral("draft_interactive"));
  options.region = scope_region_;
  if (scope_range_mode_ == QStringLiteral("Extended")) {
    options.minimum = -0.25; options.maximum = 4.0;
  } else if (scope_range_mode_ == QStringLiteral("HDR")) {
    options.maximum = 16.0;
  }

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
          .options = options,
          .configuration = scope_configuration_,
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
  // A newer requested edit does not make the currently displayed frame stale.
  // Let its scopes publish while the next render runs, but never move backward
  // or publish across asset/configuration changes.
  const auto displayed_generation = std::max(preview_session_.lastInteractiveGeneration(),
                                              preview_session_.lastFullGeneration());
  const bool publishable =
      (preview_session_.isCurrent(result.generation) || result.generation == displayed_generation) &&
      result.generation >= histogram_scope_session_.published().generation &&
      result.configuration == scope_configuration_ && !scopes_frozen_;
  WorkerQueueRegistry::logSummary(
      QStringLiteral("histogram-scope"),
      QStringLiteral("histogram"),
      QStringLiteral("edit"),
      0,
      had_pending_request ? 1 : 0,
      1,
      0,
      result.elapsed_ms,
      publishable ? 1 : 0,
      publishable ? 0 : 1);
  if (publishable) {
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
