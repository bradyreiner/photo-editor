#include "thumbnail_refresh_coordinator.h"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::ThumbnailRefreshQueueResult result_for(
    const QString& job_id,
    const QString& queue_signature,
    const QString& asset_id,
    const QString& file_path,
    const QString& state_signature,
    int state_revision) {
  using namespace photo_editor::desktop;
  return ThumbnailRefreshQueueResult{
      .request = ThumbnailRefreshQueueRequest{
          .job = AsyncMaskJob{
              .job_id = job_id,
              .asset_id = asset_id,
              .file_path = file_path,
              .state_signature = state_signature,
              .state_revision = state_revision,
          },
          .reason = QStringLiteral("mask_edit"),
          .queue_signature = queue_signature,
          .mask_thumbnail_dirty = true,
      },
      .elapsed_ms = 7,
      .mask_thumbnail_entry_count = 2,
      .service_contract = thumbnail_refresh_queue_worker_service_contract(),
      .success = true,
  };
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using Coordinator = photo_editor::desktop::ThumbnailRefreshCoordinator;

  const auto no_blockers = Coordinator::admissionBlockers({});
  if (!no_blockers.isEmpty()) {
    return fail("idle thumbnail admission facts must not produce blockers");
  }
  Coordinator::AdmissionFacts admission_facts;
  admission_facts.full_preview_running = true;
  admission_facts.live_brush_stroke = true;
  admission_facts.smart_selection_commit_worker_running = true;
  const auto admission_blockers = Coordinator::admissionBlockers(admission_facts);
  if (admission_blockers !=
      QStringList{
          QStringLiteral("full_preview_render"),
          QStringLiteral("live_brush_stroke"),
          QStringLiteral("smart_selection_commit_worker")}) {
    return fail("thumbnail admission policy must be complete, stable, and priority ordered");
  }

  auto dispatch_count = 0;
  auto completion_count = 0;
  photo_editor::desktop::ThumbnailRefreshCoordinator coordinator(
      &app,
      10000,
      [&dispatch_count]() { ++dispatch_count; },
      [&completion_count]() { ++completion_count; });

  coordinator.schedule(QStringLiteral("first"), QStringLiteral("signature-a"));
  if (!coordinator.pending() || coordinator.pendingGeneration() != 1 ||
      coordinator.reason() != QStringLiteral("first") ||
      coordinator.scheduledCount() != 1) {
    return fail("initial scheduling must publish one complete pending identity");
  }

  coordinator.schedule(QStringLiteral("coalesced"), QStringLiteral("signature-a"));
  if (coordinator.pendingGeneration() != 1 ||
      coordinator.reason() != QStringLiteral("coalesced") ||
      coordinator.cancelledCount() != 0) {
    return fail("same-signature scheduling must coalesce without superseding the generation");
  }

  const auto first_cancellation = coordinator.activate(QStringLiteral("job-a"));
  if (first_cancellation == nullptr ||
      first_cancellation->load(std::memory_order_relaxed) ||
      coordinator.activeJobId() != QStringLiteral("job-a") ||
      coordinator.activeSignature() != QStringLiteral("signature-a")) {
    return fail("activation must atomically bind job, signature, and cancellation identity");
  }

  coordinator.schedule(QStringLiteral("replacement"), QStringLiteral("signature-b"));
  if (!first_cancellation->load(std::memory_order_relaxed) ||
      coordinator.pendingGeneration() != 2 || coordinator.cancelledCount() != 1 ||
      coordinator.cancelledReason() !=
          QStringLiteral("superseded_pending_thumbnail_refresh")) {
    return fail("superseding work must cancel the active identity and advance the generation");
  }

  coordinator.defer();
  if (coordinator.preemptedCount() != 1) {
    return fail("deferred dispatch must be accounted by the coordinator");
  }

  const auto second_cancellation = coordinator.activate(QStringLiteral("job-b"));
  const auto current_result = result_for(
      QStringLiteral("job-b"),
      QStringLiteral("signature-b"),
      QStringLiteral("asset"),
      QStringLiteral("/tmp/image.raw"),
      QStringLiteral("state"),
      12);
  if (second_cancellation == nullptr ||
      !coordinator.resultIsCurrent(
          current_result,
          QStringLiteral("asset"),
          QStringLiteral("/tmp/image.raw"),
          QStringLiteral("state"),
          12)) {
    return fail("current-result validation must cover queue, asset, file, recipe, and revision");
  }

  coordinator.recordWorkerCompleted();
  coordinator.complete(current_result);
  if (coordinator.pending() || !coordinator.activeJobId().isEmpty() ||
      coordinator.dispatchedCount() != 1 ||
      coordinator.workerCompletedCount() != 1 ||
      coordinator.completion().job_id != QStringLiteral("job-b") ||
      coordinator.completion().mask_thumbnail_entry_count != 2) {
    return fail("completion must consume queue identity and retain publication accounting");
  }

  auto stale_result = current_result;
  stale_result.request.job.job_id = QStringLiteral("stale-job");
  if (coordinator.resultIsCurrent(
          stale_result,
          QStringLiteral("asset"),
          QStringLiteral("/tmp/image.raw"),
          QStringLiteral("state"),
          12)) {
    return fail("mismatched worker identity must never be current");
  }
  coordinator.recordStaleResult(stale_result);
  if (coordinator.staleResultCount() != 1) {
    return fail("stale worker completion must be accounted centrally");
  }

  if (!coordinator.consumeStaleWarning(QStringLiteral("stale")) ||
      coordinator.consumeStaleWarning(QStringLiteral("stale")) ||
      coordinator.consumeStaleWarning(QString())) {
    return fail("stale warning logging must reject empty and duplicate identities");
  }

  const auto fields = coordinator.fields(
      QStringList{QStringLiteral("full_preview_render")});
  if (fields.value(QStringLiteral("thumbnailQueuePending")).toBool() ||
      fields.value(QStringLiteral("thumbnailQueueStaleResultCount")).toInt() != 1 ||
      fields.value(QStringLiteral("thumbnailQueueDeferBlockerSignature")).toString() !=
          QStringLiteral("full_preview_render")) {
    return fail("diagnostics must be derived from coordinator-owned lifecycle state");
  }

  coordinator.reset();
  if (coordinator.pending() || !coordinator.completion().job_id.isEmpty() ||
      dispatch_count != 0 || completion_count != 0) {
    return fail("reset must cancel queued lifecycle state without invoking stages");
  }

  return EXIT_SUCCESS;
}
