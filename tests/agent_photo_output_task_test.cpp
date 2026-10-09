#include "agent_photo_output_task.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>

using namespace photo_editor::desktop;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      std::cerr << message << '\n'; \
      return false; \
    } \
  } while (false)

namespace {

constexpr auto kTaskId = "photo-output-contract";
constexpr auto kAssetId = "source-asset";

struct NativeHarness final {
  bool busy = false;
  bool cancelled = false;
  bool accept_start = true;
  bool busy_after_start = true;
  int start_count = 0;
  int cancel_count = 0;
  QString staging_path;
  QString source_revision = QStringLiteral("source-v1");
  QString layer_revision = QStringLiteral("layer-v1");
  QString approved_destination_id = QStringLiteral("destination-v1");
  QString authorized_output_path;
  bool reject_terminal_snapshot = false;
  bool empty_terminal_snapshot = false;
  std::function<void()> before_publication;
};

bool writeImage(const QString& path, const QByteArray& format) {
  QImage image(13, 9, QImage::Format_ARGB32);
  image.fill(QColor(33, 116, 207, 255));
  QImageWriter writer(path, format);
  return writer.write(image);
}

QVariantMap failed(const QString& message) {
  return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool enrollSource(AgentTaskCoordinator& tasks, const QString& journal_root, QByteArray& recipe) {
  if (!tasks.begin(journal_root, kTaskId).value(QStringLiteral("ok")).toBool()) return false;
  AgentTaskJournal::ResourceAdapter source;
  source.read = [&recipe] { return recipe; };
  source.restore = [&recipe](const QByteArray& baseline) {
    recipe = baseline;
    return true;
  };
  return tasks.enroll(kAssetId, std::move(source)).value(QStringLiteral("ok")).toBool();
}

std::unique_ptr<AgentPhotoOutputTask> makeOwner(
    AgentTaskCoordinator& tasks, NativeHarness& native, const QString& approved_path) {
  const QFileInfo requested_output(approved_path);
  const auto canonical_output_root = QFileInfo(requested_output.absolutePath()).canonicalFilePath();
  const auto canonical_approved_path = QDir(canonical_output_root).filePath(requested_output.fileName());
  native.authorized_output_path = canonical_approved_path;
  return std::make_unique<AgentPhotoOutputTask>(tasks,
      [&native, canonical_approved_path](const QString& task, const QString& asset, const QString& path,
                      const QString& operation, const QString& layer) {
        if (task != kTaskId || asset != kAssetId || path != canonical_approved_path ||
            path != native.authorized_output_path ||
            (operation != QStringLiteral("flattened") && operation != QStringLiteral("layer")) ||
            (operation == QStringLiteral("layer") && layer.isEmpty()))
          return failed(QStringLiteral("The active task source or approved destination changed."));
        return QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("approvedSnapshot"), QVariantMap{
                {QStringLiteral("taskId"), task}, {QStringLiteral("assetId"), asset},
                {QStringLiteral("outputPath"), path}, {QStringLiteral("operation"), operation},
                {QStringLiteral("layerId"), layer},
                {QStringLiteral("sourceRevision"), native.source_revision},
                {QStringLiteral("layerRevision"), operation == QStringLiteral("layer") ? native.layer_revision : QString()},
                {QStringLiteral("destinationAuthorization"), native.approved_destination_id}}}};
      },
      [&native](const QString&, const QString&, const QString& stage) {
        ++native.start_count;
        native.staging_path = stage;
        native.busy = native.busy_after_start;
        return native.accept_start;
      },
      [&native] {
        ++native.cancel_count;
        native.cancelled = true;
        return true;
      },
      [&native] { return native.busy; },
      [&native](const QString& task, const QString& asset, const QString& path,
          const QString& operation, const QString& layer, const QVariantMap&) {
        if (native.before_publication) native.before_publication();
        if (native.reject_terminal_snapshot)
          return failed(QStringLiteral("The terminal export snapshot could not be verified."));
        if (task != kTaskId || asset != kAssetId || path != native.authorized_output_path)
          return failed(QStringLiteral("The completed export no longer matches its approved target."));
        if (native.empty_terminal_snapshot)
          return QVariantMap{{QStringLiteral("ok"), true},
              {QStringLiteral("currentSnapshot"), QVariantMap{}}};
        return QVariantMap{{QStringLiteral("ok"), true},
            {QStringLiteral("currentSnapshot"), QVariantMap{
                {QStringLiteral("taskId"), task}, {QStringLiteral("assetId"), asset},
                {QStringLiteral("outputPath"), path}, {QStringLiteral("operation"), operation},
                {QStringLiteral("layerId"), layer},
                {QStringLiteral("sourceRevision"), native.source_revision},
                {QStringLiteral("layerRevision"), operation == QStringLiteral("layer") ? native.layer_revision : QString()},
                {QStringLiteral("destinationAuthorization"), native.approved_destination_id}}}};
      });
}

QVariantMap completeAndWait(AgentPhotoOutputTask& owner, NativeHarness& native,
    bool success, bool cancelled, const QString& error = {}) {
  QVariantMap result;
  bool received = false;
  QEventLoop loop;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  owner.setFinishedHandler([&](const QVariantMap& value) {
    result = value;
    received = true;
    loop.quit();
  });
  native.busy = false;
  const auto receipt = owner.nativeExportFinished(success, cancelled, native.staging_path, error);
  if (receipt.value(QStringLiteral("pending")).toBool()) {
    deadline.start(10000);
    loop.exec();
  } else if (!received) {
    result = receipt;
  }
  owner.setFinishedHandler({});
  if (!received && result.isEmpty()) result = failed(QStringLiteral("Timed out waiting for the photo output terminal receipt."));
  return result;
}

QVariantMap retrySettlementAndWait(AgentPhotoOutputTask& owner) {
  QVariantMap result;
  bool received = false;
  QEventLoop loop;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  owner.setFinishedHandler([&](const QVariantMap& value) {
    result = value;
    received = true;
    loop.quit();
  });
  const auto receipt = owner.retrySettlement(kTaskId);
  if (receipt.value(QStringLiteral("pending")).toBool()) {
    deadline.start(10000);
    loop.exec();
  } else if (!received) {
    result = receipt;
  }
  owner.setFinishedHandler({});
  if (!received && result.isEmpty()) result = failed(QStringLiteral("Timed out waiting for settlement retry."));
  return result;
}

bool successfulPngAndJpegOutputsAreJournaledAndReversible() {
  for (const auto& entry : {qMakePair(QStringLiteral("poster.png"), QByteArray("png")),
                            qMakePair(QStringLiteral("proof.jpeg"), QByteArray("jpeg"))}) {
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "Create a private test root");
    const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
    CHECK(QDir().mkpath(output_root), "Create an existing approved output folder");
    AgentTaskCoordinator tasks;
    QByteArray recipe = "approved recipe";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll the source recipe before photo output");
    NativeHarness native;
    const auto target = QDir(output_root).filePath(entry.first);
    auto owner = makeOwner(tasks, native, target);
    const auto started = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
    CHECK(started.value(QStringLiteral("ok")).toBool() &&
        started.value(QStringLiteral("accepted")).toBool() &&
        tasks.journal()->pending(owner->resourceId()),
        "Persist prepare and accepted state only after the real native start handler accepted the stage");
    CHECK(!QFileInfo::exists(target) && QFileInfo::exists(native.staging_path) == false,
        "The requested destination stays absent while rendering is active");
    CHECK(writeImage(native.staging_path, entry.second), "Write a real staged PNG/JPEG fixture");
    const auto completed = completeAndWait(*owner, native, true, false);
    CHECK(completed.value(QStringLiteral("ok")).toBool() &&
        !completed.value(QStringLiteral("pending")).toBool() &&
        completed.value(QStringLiteral("sha256")).toString().size() == 64 &&
        completed.value(QStringLiteral("byteCount")).toLongLong() > 0 &&
        completed.value(QStringLiteral("stagingRemoved")).toBool(),
        "A successful result contains the terminal receipt and removes its private stage after settlement");
    CHECK(QFileInfo::exists(target) && !owner->busy() && !QFileInfo::exists(native.staging_path),
        "The final output appears only after publication and the private stage is removed");
    QImageReader reader(target);
    CHECK(reader.canRead() && !reader.read().isNull(), "The published target is a decodable PNG/JPEG image");
    CHECK(tasks.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool(),
        "The exact output receipt passes the task review checkpoint");
    CHECK(tasks.revert().value(QStringLiteral("ok")).toBool() && !QFileInfo::exists(target) &&
        recipe == QByteArray("approved recipe"),
        "Revert removes the exact newly created output and restores the enrolled source");
  }
  return true;
}

bool stopWaitsForNativeQuiescenceAndLateReceiptCannotPublish() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private cancellation root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  AgentTaskCoordinator tasks;
  QByteArray recipe = "source baseline";
  CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll the source");
  NativeHarness native;
  const auto target = QDir(output_root).filePath(QStringLiteral("cancelled.png"));
  auto owner = makeOwner(tasks, native, target);
  const auto started = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
  CHECK(started.value(QStringLiteral("ok")).toBool(), "Start the staged export");
  CHECK(writeImage(native.staging_path, "png"), "Write the active staged output");
  const auto stop = owner->stop(kTaskId);
  CHECK(stop.value(QStringLiteral("ok")).toBool() && stop.value(QStringLiteral("stopRequested")).toBool() &&
      native.cancelled && native.cancel_count == 1 && QFileInfo::exists(native.staging_path),
      "Stop asks the real producer to cancel and retains the stage while it is active");
  CHECK(!tasks.revert().value(QStringLiteral("ok")).toBool() && QFileInfo::exists(native.staging_path) &&
      !QFileInfo::exists(target),
      "Revert cannot finish while the canceled native producer is still busy");
  const auto stopped = completeAndWait(*owner, native, false, true, QStringLiteral("Export cancelled."));
  CHECK(!stopped.value(QStringLiteral("ok")).toBool() &&
      !stopped.value(QStringLiteral("pending")).toBool() &&
      !QFileInfo::exists(target) && QFileInfo::exists(native.staging_path),
      "Only the actual terminal cancellation clears pending work; recovery bytes remain available");
  CHECK(!owner->nativeExportFinished(true, false, native.staging_path).value(QStringLiteral("ok")).toBool() &&
      !QFileInfo::exists(target),
      "A late duplicate callback cannot publish after the task has settled");
  CHECK(tasks.revert().value(QStringLiteral("ok")).toBool() && !QFileInfo::exists(target),
      "A settled cancellation is reversible");
  return true;
}

bool destinationRacesAndPreexistingFilesArePreserved() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private conflict root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  AgentTaskCoordinator tasks;
  QByteArray recipe = "source baseline";
  CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source before target checks");
  NativeHarness native;
  const auto target = QDir(output_root).filePath(QStringLiteral("manual.png"));
  const QByteArray manual_bytes = "manual image replacement";
  {
    QFile file(target);
    CHECK(file.open(QIODevice::WriteOnly) && file.write(manual_bytes) == manual_bytes.size(),
        "Create a preexisting user file");
  }
  auto owner = makeOwner(tasks, native, target);
  const auto rejected = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
  CHECK(!rejected.value(QStringLiteral("ok")).toBool() && QFileInfo::exists(target) &&
      QFileInfo(target).size() == manual_bytes.size() && native.start_count == 0,
      "An explicitly existing destination is rejected without opening the native producer");
  QFile::remove(target);

  const auto raced_target = QDir(output_root).filePath(QStringLiteral("race.jpg"));
  auto race_owner = makeOwner(tasks, native, raced_target);
  const auto started = race_owner->start(kTaskId, kAssetId, raced_target, QStringLiteral("flattened"));
  CHECK(started.value(QStringLiteral("ok")).toBool(), "Start after an absent destination was approved");
  CHECK(writeImage(native.staging_path, "jpeg"), "Produce a staged JPEG");
  const QByteArray raced_bytes = "user created this while render was active";
  {
    QFile file(raced_target);
    CHECK(file.open(QIODevice::WriteOnly) && file.write(raced_bytes) == raced_bytes.size(),
        "Create a manual target after task admission");
  }
  const auto conflict = completeAndWait(*race_owner, native, true, false);
  CHECK(!conflict.value(QStringLiteral("ok")).toBool() &&
      conflict.value(QStringLiteral("recoveryPath")).toString() == native.staging_path &&
      conflict.value(QStringLiteral("recoveryRequired")).toBool() &&
      conflict.value(QStringLiteral("pending")).toBool() && tasks.journal()->pending(race_owner->resourceId()),
      "A create-if-absent conflict preserves the stage and leaves the accepted journal pending");
  QFile preserved(raced_target);
  CHECK(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == raced_bytes,
      "The user-created target is not overwritten");
  CHECK(!tasks.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool() &&
      !tasks.revert().value(QStringLiteral("ok")).toBool() && QFileInfo::exists(raced_target),
      "The manual conflict blocks finish and Revert without deleting the user file");
  QFile::remove(raced_target);
  const auto reconciled = retrySettlementAndWait(*race_owner);
  CHECK(!reconciled.value(QStringLiteral("ok")).toBool() &&
      !reconciled.value(QStringLiteral("pending")).toBool() &&
      !tasks.journal()->pending(race_owner->resourceId()),
      "Settlement clears only after the destination is restored to its exact absent baseline");
  CHECK(tasks.revert().value(QStringLiteral("ok")).toBool(),
      "After the conflict is resolved, the exact absent output checkpoint can be reverted");
  return true;
}

bool sourceChangesDuringRenderPreventPublicationAndKeepRecoveryBytes() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private stale-source root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  AgentTaskCoordinator tasks;
  QByteArray recipe = "approved source recipe";
  CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source before output admission");
  NativeHarness native;
  const auto target = QDir(output_root).filePath(QStringLiteral("stale.png"));
  auto owner = makeOwner(tasks, native, target);
  const auto started = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
  CHECK(started.value(QStringLiteral("ok")).toBool(), "Start from the approved source snapshot");
  CHECK(writeImage(native.staging_path, "png"), "Write a complete staged output");

  recipe = "manual source edit during export";
  native.source_revision = QStringLiteral("source-v2");
  const auto stale = completeAndWait(*owner, native, true, false);
  CHECK(!stale.value(QStringLiteral("ok")).toBool() &&
      stale.value(QStringLiteral("snapshotChanged")).toBool() &&
      stale.value(QStringLiteral("conflict")).toBool() &&
      stale.value(QStringLiteral("recoveryPath")).toString() == native.staging_path,
      "A terminal-time source mismatch fails closed and identifies the retained staged output");
  CHECK(!QFileInfo::exists(target) && QFileInfo::exists(native.staging_path),
      "A stale render is never published and its useful recovery bytes remain available");
  CHECK(!tasks.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool() &&
      !tasks.revert().value(QStringLiteral("ok")).toBool() &&
      recipe == QByteArray("manual source edit during export"),
      "The changed source blocks task finish and Revert without losing the user's edit");
  return true;
}

bool postRenderLayerAndDestinationSnapshotChangesPreventPublication() {
  for (const auto& scenario : {QStringLiteral("layer"), QStringLiteral("destination")}) {
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "Create a private snapshot-conflict root");
    const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
    CHECK(QDir().mkpath(output_root), "Create output folder");
    AgentTaskCoordinator tasks;
    QByteArray recipe = "unchanged source recipe";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll unchanged source recipe");
    NativeHarness native;
    const auto target = QDir(output_root).filePath(scenario + QStringLiteral(".png"));
    auto owner = makeOwner(tasks, native, target);
    const bool layer_output = scenario == QStringLiteral("layer");
    const auto started = owner->start(kTaskId, kAssetId, target,
        layer_output ? QStringLiteral("layer") : QStringLiteral("flattened"),
        layer_output ? QStringLiteral("foreground") : QString{});
    CHECK(started.value(QStringLiteral("ok")).toBool(), "Start from an exact source/layer/destination snapshot");
    CHECK(writeImage(native.staging_path, "png"), "Write staged output before changing snapshot data");
    if (layer_output) {
      native.layer_revision = QStringLiteral("layer-v2");
    } else {
      native.approved_destination_id = QStringLiteral("destination-revoked");
    }
    const auto rejected = completeAndWait(*owner, native, true, false);
    CHECK(!rejected.value(QStringLiteral("ok")).toBool() &&
        rejected.value(QStringLiteral("snapshotChanged")).toBool() &&
        QFileInfo::exists(native.staging_path) && !QFileInfo::exists(target),
        "A changed layer or revoked output approval preserves the stage and prevents publication");
    CHECK(recipe == QByteArray("unchanged source recipe"),
        "Snapshot validation does not mutate the source document");
  }
  return true;
}

bool failedOrEmptyCompletionSnapshotPreservesStagedOutput() {
  for (const auto& scenario : {QStringLiteral("rejected"), QStringLiteral("empty")}) {
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "Create a private validation-failure root");
    const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
    CHECK(QDir().mkpath(output_root), "Create output folder");
    AgentTaskCoordinator tasks;
    QByteArray recipe = "source baseline";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source before export");
    NativeHarness native;
    native.reject_terminal_snapshot = scenario == QStringLiteral("rejected");
    native.empty_terminal_snapshot = scenario == QStringLiteral("empty");
    const auto target = QDir(output_root).filePath(scenario + QStringLiteral(".jpg"));
    auto owner = makeOwner(tasks, native, target);
    CHECK(owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"))
        .value(QStringLiteral("ok")).toBool(), "Start from a valid admission snapshot");
    CHECK(writeImage(native.staging_path, "jpeg"), "Write a valid staged JPEG");
    const auto rejected = completeAndWait(*owner, native, true, false);
    CHECK(!rejected.value(QStringLiteral("ok")).toBool() &&
        rejected.value(QStringLiteral("validationFailed")).toBool() &&
        QFileInfo::exists(native.staging_path) && !QFileInfo::exists(target),
        "A false or empty terminal snapshot callback never certifies or publishes an output");
  }
  return true;
}

bool cancellationAfterHashAndBeforePublicationPreventsOutput() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private publication-cancellation root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  AgentTaskCoordinator tasks;
  QByteArray recipe = "source baseline";
  CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source");
  NativeHarness native;
  const auto target = QDir(output_root).filePath(QStringLiteral("cancel-before-link.png"));
  auto owner = makeOwner(tasks, native, target);
  QVariantMap stop_result;
  native.before_publication = [&] { stop_result = owner->stop(kTaskId); };
  CHECK(owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"))
      .value(QStringLiteral("ok")).toBool(), "Start asynchronous staged output");
  CHECK(writeImage(native.staging_path, "png"), "Write complete staged output");
  const auto stopped = completeAndWait(*owner, native, true, false);
  CHECK(stop_result.value(QStringLiteral("ok")).toBool() &&
      stop_result.value(QStringLiteral("stopRequested")).toBool() &&
      !stopped.value(QStringLiteral("ok")).toBool() &&
      !QFileInfo::exists(target) && QFileInfo::exists(native.staging_path),
      "Stop after hashing but before atomic publication leaves no target and retains staged bytes");
  CHECK(!owner->nativeExportFinished(true, false, native.staging_path).value(QStringLiteral("ok")).toBool() &&
      !QFileInfo::exists(target),
      "A duplicate late completion cannot publish a canceled task output");
  return true;
}

bool rejectedAndUncertainNativeAdmissionHaveDifferentReceipts() {
  {
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "Create a private native-rejection root");
    const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
    CHECK(QDir().mkpath(output_root), "Create output folder");
    AgentTaskCoordinator tasks;
    QByteArray recipe = "source baseline";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source");
    NativeHarness native;
    native.accept_start = false;
    native.busy_after_start = false;
    const auto target = QDir(output_root).filePath(QStringLiteral("rejected.png"));
    auto owner = makeOwner(tasks, native, target);
    const auto rejected = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
    CHECK(!rejected.value(QStringLiteral("ok")).toBool() &&
        !rejected.value(QStringLiteral("pending")).toBool() && !owner->busy() &&
        tasks.journal()->report().value(QStringLiteral("steps")).toList().last().toMap()
            .value(QStringLiteral("status")).toString() == QStringLiteral("failed"),
        "A definitely rejected native start records a failed prepare without claiming acceptance");
  }

  {
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "Create a private uncertain-admission root");
    const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
    CHECK(QDir().mkpath(output_root), "Create output folder");
    AgentTaskCoordinator tasks;
    QByteArray recipe = "source baseline";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source");
    NativeHarness native;
    native.accept_start = false;
    native.busy_after_start = true;
    const auto target = QDir(output_root).filePath(QStringLiteral("uncertain.png"));
    auto owner = makeOwner(tasks, native, target);
    const auto uncertain = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
    CHECK(!uncertain.value(QStringLiteral("ok")).toBool() &&
        uncertain.value(QStringLiteral("pending")).toBool() &&
        uncertain.value(QStringLiteral("recoveryRequired")).toBool() && owner->busy() &&
        native.cancelled && native.cancel_count == 1,
        "A false admission with a live producer remains pending and requests cancellation");
    CHECK(writeImage(native.staging_path, "png"), "Write partial output before native quiescence");
    native.busy = false;
    const auto terminal = owner->nativeExportFinished(false, true, native.staging_path,
        QStringLiteral("Stopped before acceptance was confirmed."));
    CHECK(!terminal.value(QStringLiteral("ok")).toBool() &&
        !terminal.value(QStringLiteral("pending")).toBool() &&
        !owner->busy() && QFileInfo::exists(native.staging_path) &&
        tasks.journal()->report().value(QStringLiteral("steps")).toList().last().toMap()
            .value(QStringLiteral("status")).toString() == QStringLiteral("failed"),
        "Only the actual terminal callback settles uncertain native work as failed and retains partial bytes");
    CHECK(tasks.revert().value(QStringLiteral("ok")).toBool() && !QFileInfo::exists(target) &&
        recipe == QByteArray("source baseline"),
        "A safely settled uncertain admission remains reversible without changing the source");
  }
  return true;
}

bool recoveredUnreceiptedDestinationConflictRetainsAcceptedPendingStep() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private interrupted-output root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  const auto target = QDir(output_root).filePath(QStringLiteral("interrupted.png"));
  const QByteArray manual_bytes = "unreceipted destination bytes";
  NativeHarness native;
  QString stage_path;
  {
    AgentTaskCoordinator tasks;
    QByteArray recipe = "source baseline";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll the source before the interrupted export");
    auto owner = makeOwner(tasks, native, target);
    const auto started = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
    CHECK(started.value(QStringLiteral("ok")).toBool() &&
        tasks.journal()->pending(owner->resourceId()),
        "Persist an accepted output step before simulating process loss");
    stage_path = native.staging_path;
    CHECK(writeImage(stage_path, "png"), "Keep rendered recovery bytes before process loss");
    QFile manual(target);
    CHECK(manual.open(QIODevice::WriteOnly) && manual.write(manual_bytes) == manual_bytes.size(),
        "Create an unreceipted destination while the output is pending");
    native.busy = false;
  }

  AgentTaskCoordinator recovered;
  CHECK(recovered.recover(temporary.path(), kTaskId).value(QStringLiteral("ok")).toBool(),
      "Recover the accepted journal after simulated process loss");
  NativeHarness idle;
  auto owner = makeOwner(recovered, idle, target);
  const auto conflict = owner->reattach(kTaskId, kAssetId, target);
  CHECK(!conflict.value(QStringLiteral("ok")).toBool() &&
      conflict.value(QStringLiteral("recoveryRequired")).toBool() &&
      conflict.value(QStringLiteral("journalPending")).toBool() &&
      recovered.journal()->pending(owner->resourceId()) && QFileInfo::exists(stage_path),
      "Recovery keeps the accepted step pending when current destination bytes have no task receipt");
  QFile preserved(target);
  CHECK(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == manual_bytes,
      "Recovery preserves the unreceipted destination byte for byte");
  QFile::remove(target);

  QByteArray recipe = "source baseline";
  AgentTaskJournal::ResourceAdapter source;
  source.read = [&recipe] { return recipe; };
  source.restore = [&recipe](const QByteArray& baseline) { recipe = baseline; return true; };
  CHECK(recovered.enroll(kAssetId, std::move(source)).value(QStringLiteral("ok")).toBool(),
      "Reattach the source adapter");
  const auto absent = owner->reattach(kTaskId, kAssetId, target);
  CHECK(!absent.value(QStringLiteral("ok")).toBool() &&
      !recovered.journal()->pending(owner->resourceId()),
      "Only the exact absent baseline permits a failed terminal settlement after recovery");
  CHECK(recovered.revert().value(QStringLiteral("ok")).toBool() &&
      !QFileInfo::exists(target) && recipe == QByteArray("source baseline"),
      "Once the conflict is resolved, exact task Revert succeeds");
  return true;
}

bool outputReceiptReattachesAcrossTaskRecovery() {
  QTemporaryDir temporary;
  CHECK(temporary.isValid(), "Create a private recovery root");
  const auto output_root = QDir(temporary.path()).filePath(QStringLiteral("exports"));
  CHECK(QDir().mkpath(output_root), "Create output folder");
  const auto target = QDir(output_root).filePath(QStringLiteral("recovered.png"));
  NativeHarness native;
  QString staging_path;
  {
    AgentTaskCoordinator tasks;
    QByteArray recipe = "source baseline";
    CHECK(enrollSource(tasks, temporary.path(), recipe), "Enroll source");
    auto owner = makeOwner(tasks, native, target);
    const auto started = owner->start(kTaskId, kAssetId, target, QStringLiteral("flattened"));
    CHECK(started.value(QStringLiteral("ok")).toBool(), "Start output before simulated restart");
    staging_path = native.staging_path;
    CHECK(writeImage(staging_path, "png"), "Write staged PNG before terminal receipt");
    const auto completed = completeAndWait(*owner, native, true, false);
    CHECK(completed.value(QStringLiteral("ok")).toBool() &&
        tasks.finish(QStringLiteral("review")).value(QStringLiteral("ok")).toBool(),
        "Persist the completed output receipt before recovery");
  }

  AgentTaskCoordinator recovered;
  CHECK(recovered.recover(temporary.path(), kTaskId).value(QStringLiteral("ok")).toBool(),
      "Recover the durable task journal");
  NativeHarness idle;
  idle.busy = false;
  auto recovered_owner = makeOwner(recovered, idle, target);
  idle.source_revision = QStringLiteral("source-v2");
  const auto stale_source = recovered_owner->reattach(kTaskId, kAssetId, target);
  CHECK(!stale_source.value(QStringLiteral("ok")).toBool() &&
      stale_source.value(QStringLiteral("sourceSnapshotChanged")).toBool() && QFileInfo::exists(target),
      "Recovery pins and revalidates the original source snapshot without deleting output on mismatch");
  idle.source_revision = QStringLiteral("source-v1");
  const auto reattached = recovered_owner->reattach(kTaskId, kAssetId, target);
  CHECK(reattached.value(QStringLiteral("ok")).toBool() &&
      reattached.value(QStringLiteral("reattached")).toBool(),
      "Reattach only the exact file whose live snapshot matches the persisted checkpoint");
  QByteArray recipe = "source baseline";
  AgentTaskJournal::ResourceAdapter source;
  source.read = [&recipe] { return recipe; };
  source.restore = [&recipe](const QByteArray& baseline) { recipe = baseline; return true; };
  CHECK(recovered.enroll(kAssetId, std::move(source)).value(QStringLiteral("ok")).toBool(),
      "Reattach the original enrolled source");
  CHECK(recovered.revert().value(QStringLiteral("ok")).toBool() && !QFileInfo::exists(target),
      "Recovered exact output remains safely reversible");
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!successfulPngAndJpegOutputsAreJournaledAndReversible() ||
      !stopWaitsForNativeQuiescenceAndLateReceiptCannotPublish() ||
      !destinationRacesAndPreexistingFilesArePreserved() ||
      !sourceChangesDuringRenderPreventPublicationAndKeepRecoveryBytes() ||
      !postRenderLayerAndDestinationSnapshotChangesPreventPublication() ||
      !failedOrEmptyCompletionSnapshotPreservesStagedOutput() ||
      !cancellationAfterHashAndBeforePublicationPreventsOutput() ||
      !rejectedAndUncertainNativeAdmissionHaveDifferentReceipts() ||
      !recoveredUnreceiptedDestinationConflictRetainsAcceptedPendingStep() ||
      !outputReceiptReattachesAcrossTaskRecovery()) return 1;
  std::cout << "Task-owned staged photo outputs, receipts, Stop, Revert and recovery passed.\n";
  return 0;
}
