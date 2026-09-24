#include "mask_worker_coordinator.h"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  photo_editor::desktop::MaskWorkerCoordinator coordinator;
  const auto preview_job = photo_editor::desktop::AsyncMaskJob{
      .job_id = QStringLiteral("preview-job"),
      .generation = 4,
  };
  coordinator.setLatestSmartSelectPreviewJob(preview_job);
  coordinator.setPendingSmartSelectPreview(
      photo_editor::desktop::SmartSelectPreviewRequest{
          .job = preview_job,
          .file_path = QStringLiteral("/tmp/source.raw"),
          .asset_id = QStringLiteral("asset-a"),
          .points = QVariantList{QVariantMap{{QStringLiteral("x"), 0.25}}},
          .generation = 4,
      });
  if (!coordinator.smartSelectPreviewPending() ||
      coordinator.pendingSmartSelectPreview().job.job_id !=
          QStringLiteral("preview-job") ||
      coordinator.latestSmartSelectPreviewJob().generation != 4) {
    return fail("smart-selection preview payload and latest identity must queue atomically");
  }
  coordinator.markSmartSelectPreviewDispatched();
  if (coordinator.smartSelectPreviewPending() ||
      coordinator.pendingSmartSelectPreview().points.isEmpty()) {
    return fail("dispatch must retain payload identity for duplicate and stale checks");
  }
  coordinator.clearPendingSmartSelectPreview();
  if (coordinator.smartSelectPreviewPending() ||
      !coordinator.pendingSmartSelectPreview().points.isEmpty()) {
    return fail("asset cancellation must clear the queued preview payload");
  }

  const auto commit_job = photo_editor::desktop::AsyncMaskJob{
      .job_id = QStringLiteral("commit-job"),
      .generation = 7,
  };
  coordinator.setLatestSmartSelectCommitJob(commit_job);
  coordinator.setPendingSmartSelectCommit(
      photo_editor::desktop::SmartSelectCommitRequest{
          .job = commit_job,
          .intent = QStringLiteral("remove"),
          .create_new = true,
          .generation = 7,
          .state_revision = 11,
      });
  if (!coordinator.smartSelectCommitPending() ||
      coordinator.pendingSmartSelectCommit().intent != QStringLiteral("remove") ||
      !coordinator.pendingSmartSelectCommit().create_new) {
    return fail("smart-selection commit payload must retain its complete publication contract");
  }
  coordinator.markSmartSelectCommitDispatched();
  if (coordinator.smartSelectCommitPending()) {
    return fail("commit dispatch must atomically consume pending state");
  }

  coordinator.setLatestBrushRasterJob(
      photo_editor::desktop::AsyncMaskJob{.job_id = QStringLiteral("brush-job")});
  coordinator.setLatestInpaintJob(
      photo_editor::desktop::AsyncMaskJob{.job_id = QStringLiteral("inpaint-job")});
  coordinator.latestBrushRasterGeneration()->store(9, std::memory_order_relaxed);
  if (coordinator.latestBrushRasterJob().job_id != QStringLiteral("brush-job") ||
      coordinator.latestInpaintJob().job_id != QStringLiteral("inpaint-job") ||
      coordinator.latestBrushRasterGeneration()->load(std::memory_order_relaxed) != 9 ||
      coordinator.smartSelectPreviewWatcher().isRunning() ||
      coordinator.smartSelectCommitWatcher().isRunning() ||
      coordinator.brushRasterCommitWatcher().isRunning() ||
      coordinator.inpaintApplyWatcher().isRunning() ||
      coordinator.inpaintRegenerateWatcher().isRunning()) {
    return fail("all five mask worker watchers and latest job identities must be coordinator-owned");
  }

  return EXIT_SUCCESS;
}
