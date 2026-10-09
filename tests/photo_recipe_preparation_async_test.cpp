#include "app_bootstrap.h"
#include "editor_preview_controller.h"
#include "editor_recipe_save_journal.h"
#include "agent_task_recipe_persistence.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <utility>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static bool initializeJournal(EditorPreviewController& controller) {
    return controller.ensureRecipeSaveJournal();
  }
  static void enqueue(EditorPreviewController& controller, EditorRecipeSaveSnapshot snapshot) {
    controller.recipe_save_queue_.enqueue(std::move(snapshot));
  }
  static void flush(EditorPreviewController& controller) {
    controller.flushPersistedState();
  }
  static bool checkpointWrites(EditorPreviewController& controller) {
    return controller.checkpointRecipeSaves();
  }
  static bool preparing(const EditorPreviewController& controller) {
    return controller.recipe_preparation_active_;
  }
  static EditorRecipeSaveCheckpoint checkpoint(const EditorPreviewController& controller) {
    return controller.recipe_save_queue_.checkpoint();
  }
  static QString journalPath(const EditorPreviewController& controller) {
    return controller.recipe_save_journal_.path();
  }
  static bool timerActive(const EditorPreviewController& controller) {
    return controller.persist_timer_.isActive();
  }
  static QProcess::ProcessState processState(const EditorPreviewController& controller) {
    return controller.persist_process_.state();
  }
  static void pause(EditorPreviewController& controller, bool paused) {
    controller.pauseRecipePersistence(paused);
  }
};
}

namespace {
using namespace photo_editor::desktop;

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

void configure_runtime(const QString& root) {
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", QDir(root).filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_BACKUP_DIR", QDir(root).filePath(QStringLiteral("backups")).toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", QDir(root).filePath(QStringLiteral("managed")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://looksy-test.invalid/no-database"));
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
}

bool wait_until(const std::function<bool()>& condition, int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  return condition();
}

EditorRecipeSaveSnapshot snapshot(const QString& asset_id, const QString& image_path, double exposure) {
  EditRecipeState state;
  state.exposure = exposure;
  return {.asset_id = asset_id, .file_path = image_path, .state = std::move(state)};
}

bool has_pending(const EditorRecipeSaveCheckpoint& checkpoint, const QString& asset_id,
                 double exposure) {
  for (const auto& entry : checkpoint.pending) {
    if (entry.asset_id == asset_id) return entry.state.exposure == exposure;
  }
  return false;
}

bool has_rejected(const EditorRecipeSaveCheckpoint& checkpoint, const QString& asset_id,
                  double raw_boost) {
  for (const auto& entry : checkpoint.rejected) {
    if (entry.asset_id == asset_id) return entry.state.raw_boost == raw_boost;
  }
  return false;
}

bool recover_checkpoint_copy(const QString& source_path, const QString& recovery_root,
                             EditorRecipeSaveCheckpoint* checkpoint, QString* error) {
  const auto authority = EditorRecipeSaveJournal::currentAuthority();
  const auto namespace_hash = QString::fromLatin1(
      QCryptographicHash::hash(authority.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto directory = QDir(recovery_root).filePath(QStringLiteral("recipe-outbox"));
  if (!QDir().mkpath(directory)) return false;
  const auto destination = QDir(directory).filePath(namespace_hash + QStringLiteral(".json"));
  QFile::remove(destination);
  if (!QFile::copy(source_path, destination)) return false;
  EditorRecipeSaveJournal recovered_journal;
  return recovered_journal.open(recovery_root, authority, checkpoint, error);
}

bool make_png(const QString& path) {
  QImage image(96, 64, QImage::Format_RGBA8888);
  image.fill(QColor(60, 100, 140, 255));
  return image.save(path, "PNG");
}

bool paused_preparation_keeps_durable_latest_edits(const QTemporaryDir& root,
                                                   const QString& image_path) {
  const auto app_root = root.filePath(QStringLiteral("paused"));
  configure_runtime(app_root);
  EditorPreviewController controller;
  if (!EditorPreviewControllerTestAccess::initializeJournal(controller)) return false;
  EditorPreviewControllerTestAccess::enqueue(controller, snapshot("asset-a", image_path, 0.1));
  EditorPreviewControllerTestAccess::enqueue(controller, snapshot("asset-b", image_path, 0.2));
  EditorPreviewControllerTestAccess::flush(controller);
  if (!require(EditorPreviewControllerTestAccess::preparing(controller),
               "flush must enter worker preparation before returning") ||
      !require(EditorPreviewControllerTestAccess::checkpoint(controller).active.has_value(),
               "the active immutable snapshot must exist during preparation")) return false;

  const auto initial_copy_root = root.filePath(QStringLiteral("paused-recovery-initial"));
  EditorRecipeSaveCheckpoint initial_recovered;
  QString error;
  if (!recover_checkpoint_copy(EditorPreviewControllerTestAccess::journalPath(controller),
                               initial_copy_root, &initial_recovered, &error) ||
      !require(initial_recovered.active && initial_recovered.active->asset_id == "asset-a" &&
                   initial_recovered.active->state.exposure == 0.1,
               "reopen during preparation must recover the active snapshot") ||
      !require(has_pending(initial_recovered, "asset-b", 0.2),
               "reopen during preparation must recover other pending snapshots")) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }

  // A newer edit to the active asset supersedes its old snapshot while the
  // worker prepares; another asset remains queued behind it.
  EditorPreviewControllerTestAccess::enqueue(controller, snapshot("asset-a", image_path, 0.8));
  EditorPreviewControllerTestAccess::enqueue(controller, snapshot("asset-c", image_path, 0.3));
  if (!EditorPreviewControllerTestAccess::checkpointWrites(controller)) {
    std::cerr << "newer pending recipe checkpoint failed\n";
    return false;
  }
  EditorPreviewControllerTestAccess::pause(controller, true);
  if (!wait_until([&] { return !EditorPreviewControllerTestAccess::preparing(controller); }))
    return require(false, "paused worker preparation did not finish");
  if (!require(EditorPreviewControllerTestAccess::processState(controller) == QProcess::NotRunning,
               "close pause must prevent a late helper start") ||
      !require(!EditorPreviewControllerTestAccess::checkpoint(controller).active.has_value(),
               "paused completion must release the active queue slot")) return false;

  const auto after_pause = EditorPreviewControllerTestAccess::checkpoint(controller);
  if (!require(after_pause.pending.size() == 3 &&
                   has_pending(after_pause, "asset-a", 0.8) &&
                   has_pending(after_pause, "asset-b", 0.2) &&
                   has_pending(after_pause, "asset-c", 0.3),
               "pause must retain the latest edit and every queued asset")) return false;
  const auto paused_recovery_root = root.filePath(QStringLiteral("paused-recovery-final"));
  EditorRecipeSaveCheckpoint paused_recovered;
  if (!recover_checkpoint_copy(EditorPreviewControllerTestAccess::journalPath(controller),
                               paused_recovery_root, &paused_recovered, &error)) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  return require(!paused_recovered.active.has_value() &&
                     paused_recovered.pending.size() == 3 &&
                     has_pending(paused_recovered, "asset-a", 0.8) &&
                     has_pending(paused_recovered, "asset-b", 0.2) &&
                     has_pending(paused_recovered, "asset-c", 0.3),
                 "the post-pause durable journal must contain the newest snapshots");
}

bool quarantine_invalid_and_schedule_unrelated_work(const QTemporaryDir& root,
                                                    const QString& image_path) {
  const auto app_root = root.filePath(QStringLiteral("quarantine"));
  configure_runtime(app_root);
  EditorPreviewController controller;
  if (!EditorPreviewControllerTestAccess::initializeJournal(controller)) return false;
  auto invalid = snapshot("invalid-a", image_path, 0.4);
  invalid.state.raw_boost = 12.0; // canonical v13 rejects unsupported legacy intent
  EditorPreviewControllerTestAccess::enqueue(controller, std::move(invalid));
  EditorPreviewControllerTestAccess::enqueue(controller, snapshot("valid-b", image_path, 0.6));

  bool unrelated_resume_scheduled = false;
  const auto connection = QObject::connect(
      &controller, &EditorPreviewController::recipeSaveStateChanged, &controller, [&] {
        const auto state = EditorPreviewControllerTestAccess::checkpoint(controller);
        if (!EditorPreviewControllerTestAccess::preparing(controller) &&
            EditorPreviewControllerTestAccess::timerActive(controller) &&
            has_rejected(state, "invalid-a", 12.0) &&
            has_pending(state, "valid-b", 0.6)) {
          unrelated_resume_scheduled = true;
          // Stop the 0-ms continuation before it can invoke any catalog helper.
          EditorPreviewControllerTestAccess::pause(controller, true);
        }
      });
  EditorPreviewControllerTestAccess::flush(controller);
  if (!require(EditorPreviewControllerTestAccess::preparing(controller),
               "invalid snapshot should still be prepared asynchronously")) return false;
  if (!wait_until([&] { return unrelated_resume_scheduled; })) {
    QObject::disconnect(connection);
    return require(false, "invalid recovery was not quarantined or unrelated work was not scheduled");
  }
  QObject::disconnect(connection);
  const auto state = EditorPreviewControllerTestAccess::checkpoint(controller);
  if (!require(!state.active.has_value() && state.pending.size() == 1 &&
                   has_pending(state, "valid-b", 0.6) &&
                   has_rejected(state, "invalid-a", 12.0),
               "quarantine must preserve the bad edit and retain unrelated pending work")) return false;

  QString error;
  EditorRecipeSaveCheckpoint recovered;
  const auto recovery_root = root.filePath(QStringLiteral("quarantine-recovery"));
  if (!recover_checkpoint_copy(EditorPreviewControllerTestAccess::journalPath(controller),
                               recovery_root, &recovered, &error)) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  if (!require(has_rejected(recovered, "invalid-a", 12.0) &&
                   has_pending(recovered, "valid-b", 0.6),
               "quarantined and unrelated snapshots must both survive journal reopen")) return false;
  EditorPreviewControllerTestAccess::pause(controller, false);
  const bool resumed = EditorPreviewControllerTestAccess::timerActive(controller);
  if (!resumed) return require(false, "unpausing must schedule the valid unrelated queued save");
  EditorPreviewControllerTestAccess::flush(controller);
  if (!require(EditorPreviewControllerTestAccess::preparing(controller) &&
                   EditorPreviewControllerTestAccess::checkpoint(controller).active &&
                   EditorPreviewControllerTestAccess::checkpoint(controller).active->asset_id == "valid-b",
               "the unrelated valid snapshot must resume worker preparation")) return false;
  EditorPreviewControllerTestAccess::pause(controller, true);
  if (!wait_until([&] { return !EditorPreviewControllerTestAccess::preparing(controller); }))
    return require(false, "resumed valid preparation did not finish");
  return require(EditorPreviewControllerTestAccess::processState(controller) == QProcess::NotRunning &&
                     has_pending(EditorPreviewControllerTestAccess::checkpoint(controller), "valid-b", 0.6),
                 "the resumed unrelated edit must remain durable without a late helper start");
}

bool destroying_controller_during_preparation_is_safe(const QTemporaryDir& root,
                                                       const QString& image_path) {
  const auto app_root = root.filePath(QStringLiteral("destroy-in-flight"));
  configure_runtime(app_root);
  QString journal_path;
  {
    EditorPreviewController controller;
    if (!EditorPreviewControllerTestAccess::initializeJournal(controller)) return false;
    EditorPreviewControllerTestAccess::enqueue(controller, snapshot("destroy-a", image_path, 0.9));
    EditorPreviewControllerTestAccess::enqueue(controller, snapshot("destroy-b", image_path, 0.7));
    EditorPreviewControllerTestAccess::flush(controller);
    if (!require(EditorPreviewControllerTestAccess::preparing(controller),
                 "controller must have worker preparation in flight before destruction")) return false;
    journal_path = EditorPreviewControllerTestAccess::journalPath(controller);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  const auto recovery_root = root.filePath(QStringLiteral("destroy-recovery"));
  EditorRecipeSaveCheckpoint recovered;
  QString error;
  if (!recover_checkpoint_copy(journal_path, recovery_root, &recovered, &error)) {
    std::cerr << error.toStdString() << '\n';
    return false;
  }
  EditorRecipeSaveQueue reopened_queue;
  reopened_queue.restore(recovered);
  const auto retried = reopened_queue.checkpoint();
  return require(!retried.active.has_value() &&
                     has_pending(retried, "destroy-a", 0.9) &&
                     has_pending(retried, "destroy-b", 0.7),
                 "destroying during preparation must leave a recoverable queue without callbacks into freed state");
}

bool owned_revision_is_admitted_before_helper_launch(const QTemporaryDir& root,
                                                     const QString& image_path) {
  configure_runtime(root.filePath(QStringLiteral("owned-admission")));
  EditorPreviewController controller;
  if (!EditorPreviewControllerTestAccess::initializeJournal(controller)) return false;
  auto revision = snapshot("owned-asset", image_path, 0.625);
  revision.agent_task_id = QStringLiteral("original-task");
  QVariantMap observed;
  bool called = false;
  controller.setAgentRecipePersistenceHandler([&](const QString& stage, const QVariantMap& context) {
    if (stage == QStringLiteral("guard")) return QVariantMap{{"ok", true}, {"payload", context.value("payload")}};
    called = true;
    if (stage != QStringLiteral("prepare")) return QVariantMap{{"ok", false}};
    observed = context;
    return QVariantMap{{"ok", false}, {"error", QStringLiteral("Exact owner conflict fixture")}};
  });
  EditorPreviewControllerTestAccess::enqueue(controller, revision);
  EditorPreviewControllerTestAccess::flush(controller);
  if (!wait_until([&] { return called && !EditorPreviewControllerTestAccess::preparing(controller); }))
    return require(false, "owned revision admission did not finish");
  EditorPreviewControllerTestAccess::pause(controller, true);
  const auto state = EditorPreviewControllerTestAccess::checkpoint(controller);
  const auto exact_fields = agent_recipe_fields_from_prepared_payload(
      observed.value("payload").toByteArray(), QStringLiteral("owned-asset"));
  if (!require(observed.value("taskId") == QStringLiteral("original-task") &&
      observed.value("assetId") == QStringLiteral("owned-asset") &&
      !observed.value("nativeRecipe").toString().isEmpty() &&
      !observed.value("payload").toByteArray().isEmpty() && !exact_fields.isEmpty() &&
      agent_recipe_fields_from_prepared_payload(observed.value("payload").toByteArray(),
          QStringLiteral("another-asset")).isEmpty(),
      "admission must receive the immutable original task, asset, recipe and helper bytes") ||
      !require(EditorPreviewControllerTestAccess::processState(controller) == QProcess::NotRunning &&
      !state.active && state.pending.size() == 1 &&
      state.pending.first().agent_task_id == QStringLiteral("original-task") &&
      state.pending.first().state.exposure == 0.625 &&
      controller.recipeSaveError().contains(QStringLiteral("Exact owner conflict fixture")),
      "rejected admission must launch no helper and preserve the attributed revision")) return false;
  EditorRecipeSaveCheckpoint recovered;
  QString error;
  return recover_checkpoint_copy(EditorPreviewControllerTestAccess::journalPath(controller),
      root.filePath(QStringLiteral("owned-admission-recovery")), &recovered, &error) &&
      recovered.pending.size() == 1 && recovered.pending.first().agent_task_id == QStringLiteral("original-task");
}

bool owned_save_acceptance_requires_running_helper(const QTemporaryDir& root,
                                                   const QString& image_path) {
  configure_runtime(root.filePath(QStringLiteral("owned-process-acceptance")));
  EditorPreviewController controller;
  if (!EditorPreviewControllerTestAccess::initializeJournal(controller)) return false;
  auto revision = snapshot("owned-process-asset", image_path, 0.375);
  revision.agent_task_id = QStringLiteral("process-owner-task");
  QStringList stages;
  QByteArray immutable_payload;
  bool prepared_while_stopped = false;
  bool accepted_while_running = false;
  bool finished_unsuccessfully = false;
  bool same_revision = true;
  controller.setAgentRecipePersistenceHandler([&](const QString& stage, const QVariantMap& context) {
    stages.append(stage);
    same_revision = same_revision && context.value("taskId") == revision.agent_task_id &&
        context.value("assetId") == revision.asset_id;
    if (stage == QStringLiteral("guard"))
      return QVariantMap{{"ok", true}, {"payload", context.value("payload")}};
    if (stage == QStringLiteral("prepare")) {
      immutable_payload = context.value("payload").toByteArray();
      prepared_while_stopped = EditorPreviewControllerTestAccess::processState(controller) == QProcess::NotRunning;
      return QVariantMap{{"ok", true}};
    }
    same_revision = same_revision && context.value("payload").toByteArray() == immutable_payload;
    if (stage == QStringLiteral("accepted")) {
      accepted_while_running = EditorPreviewControllerTestAccess::processState(controller) == QProcess::Running;
      // Reject this actual helper at acceptance: no database write is authorized.
      return QVariantMap{{"ok", false}, {"error", QStringLiteral("Acceptance journal failure fixture")}};
    }
    if (stage == QStringLiteral("finished")) finished_unsuccessfully = !context.value("ok").toBool();
    return QVariantMap{{"ok", true}};
  });
  EditorPreviewControllerTestAccess::enqueue(controller, revision);
  EditorPreviewControllerTestAccess::flush(controller);
  if (!wait_until([&] {
        return finished_unsuccessfully &&
            EditorPreviewControllerTestAccess::processState(controller) == QProcess::NotRunning;
      })) return require(false, "rejected helper acceptance did not reach terminal settlement");
  EditorPreviewControllerTestAccess::pause(controller, true);
  const auto state = EditorPreviewControllerTestAccess::checkpoint(controller);
  return require(stages == QStringList{"guard", "prepare", "accepted", "finished"} &&
      prepared_while_stopped && accepted_while_running && same_revision && !immutable_payload.isEmpty() &&
      !state.active && state.pending.size() == 1 &&
      state.pending.first().agent_task_id == revision.agent_task_id &&
      state.pending.first().state.exposure == 0.375,
      "acceptance must follow actual process start and failure must retain the exact owned revision");
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const auto image_path = root.filePath(QStringLiteral("source.png"));
  if (!make_png(image_path)) return 1;
  if (!paused_preparation_keeps_durable_latest_edits(root, image_path) ||
      !quarantine_invalid_and_schedule_unrelated_work(root, image_path) ||
      !destroying_controller_during_preparation_is_safe(root, image_path) ||
      !owned_revision_is_admitted_before_helper_launch(root, image_path) ||
      !owned_save_acceptance_requires_running_helper(root, image_path)) return 1;
  std::cout << "Worker preparation, durable active/pending recovery, close pause, latest-edit supersession, invalid-recipe quarantine, unrelated-save scheduling, and in-flight destruction contracts passed.\n";
  return 0;
}
