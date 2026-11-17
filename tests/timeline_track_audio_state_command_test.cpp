#include "agent_timeline_commands.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <iostream>

namespace {

using photo_editor::desktop::AgentAppCommandRegistry;
using photo_editor::desktop::AgentTaskCoordinator;
using photo_editor::desktop::TimelineDocumentController;

bool fail(const char* message) {
  std::cerr << "timeline_track_audio_state_command_test: " << message << '\n';
  return false;
}

int track_row(const TimelineDocumentController& controller,
              const QString& track_id,
              QVariantMap* found = nullptr) {
  const auto rows = controller.snapshotTimelineTracks();
  for (int row = 0; row < rows.size(); ++row) {
    const auto value = rows.at(row).toMap();
    if (value.value(QStringLiteral("trackId")).toString() != track_id) continue;
    if (found != nullptr) *found = value;
    return row;
  }
  return -1;
}

QVariantMap command_values(const QString& track_id,
                           const QString& value_name,
                           const QVariant& desired,
                           const QString& expected_name,
                           const QVariant& expected) {
  return {{QStringLiteral("trackId"), track_id},
          {value_name, desired},
          {expected_name, expected}};
}

bool run() {
  QTemporaryDir app_data;
  QTemporaryDir journal_root;
  if (!app_data.isValid() || !journal_root.isValid()) {
    return fail("isolated app and task directories should be available");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.path().toUtf8());

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimeline(QStringLiteral("Track Audio State Adapter"));
  const auto old_music = controller.createTimelineTrackForAutomation(
      QStringLiteral("audio"), QStringLiteral("Reference Music"),
      QStringLiteral("music"));
  const auto new_music = controller.createTimelineTrackForAutomation(
      QStringLiteral("audio"), QStringLiteral("Replacement Music"),
      QStringLiteral("music"));
  const auto locked_track = controller.createTimelineTrackForAutomation(
      QStringLiteral("audio"), QStringLiteral("Locked Lane"),
      QStringLiteral("effects"));
  const auto old_id = old_music.value(QStringLiteral("trackId")).toString();
  const auto new_id = new_music.value(QStringLiteral("trackId")).toString();
  const auto locked_id = locked_track.value(QStringLiteral("trackId")).toString();
  if (!old_music.value(QStringLiteral("ok")).toBool() || old_id.isEmpty() ||
      !new_music.value(QStringLiteral("ok")).toBool() || new_id.isEmpty() ||
      !locked_track.value(QStringLiteral("ok")).toBool() || locked_id.isEmpty()) {
    return fail("audio fixture tracks should be created with stable IDs");
  }
  if (!controller.dirty()) {
    return fail("the task fixture should begin from an unsaved Timeline");
  }
  const auto locked_row = track_row(controller, locked_id);
  if (locked_row < 0) return fail("the locked test lane should be readable by ID");
  controller.toggleTrackLocked(locked_row);
  QVariantMap locked_state;
  if (track_row(controller, locked_id, &locked_state) < 0 ||
      !locked_state.value(QStringLiteral("locked")).toBool()) {
    return fail("the locked test lane should be locked before task admission");
  }
  const auto prepared = controller.prepareAgentTaskDocumentForAutomation();
  if (!prepared.value(QStringLiteral("ok")).toBool() ||
      !prepared.value(QStringLiteral("ready")).toBool() ||
      !controller.acceptAgentTaskDocumentPreparationForAutomation()
           .value(QStringLiteral("ok")).toBool()) {
    return fail("the stable Timeline should admit a task before adapter calls");
  }
  const auto task_baseline_state = controller.agentTaskDocumentState();
  const auto task_baseline_state_hash = QString::fromLatin1(
      QCryptographicHash::hash(task_baseline_state, QCryptographicHash::Sha256).toHex());

  const QString task_id = QStringLiteral("track-audio-state-task");
  const auto canonical_task_root = QDir(journal_root.path()).canonicalPath();
  const auto task_output_path =
      QDir(canonical_task_root).filePath(QStringLiteral("looksy-audio-task-copy.json"));
  AgentTaskCoordinator tasks;
  if (!tasks.begin(canonical_task_root, task_id).value(QStringLiteral("ok")).toBool()) {
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("a task journal should begin in the isolated directory");
  }
  const auto task_scope = tasks.journal()->recordNativeTimelineScope(
      {controller.agentTaskDocumentId()}, {}, canonical_task_root);
  if (!task_scope.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "Timeline task scope: "
              << QJsonDocument::fromVariant(task_scope).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the owned task should admit its canonical Timeline output directory");
  }
  AgentAppCommandRegistry registry;
  photo_editor::desktop::register_agent_timeline_commands(
      registry, &controller, tasks, [task_id] { return task_id; },
      [](const QString&) { return QVariantMap{{QStringLiteral("ok"), true}}; },
      [task_output_path](const QString& permission, const QString& path) {
        if (permission != QStringLiteral("save_project") || path != task_output_path ||
            QFileInfo::exists(path)) {
          return QVariantMap{{QStringLiteral("ok"), false},
                             {QStringLiteral("error"), QStringLiteral("The task copy path is not admitted.")}};
        }
        const auto output = AgentTaskCoordinator::taskOwnedFileResource(path);
        return QVariantMap{{QStringLiteral("ok"), !output.resource_id.isEmpty()},
                           {QStringLiteral("canonicalPath"), output.canonical_path},
                           {QStringLiteral("resourceId"), output.resource_id}};
      },
      false);

  const QString mute_command =
      QStringLiteral("timeline.action.setTimelineTrackMuted");
  const QString role_command =
      QStringLiteral("timeline.action.setTimelineTrackAudioRole");
  const auto mute_schema = registry.actionInputSchema(mute_command);
  const auto role_schema = registry.actionInputSchema(role_command);
  const auto mute_properties = mute_schema.value(QStringLiteral("inputSchema")).toMap()
                                   .value(QStringLiteral("properties")).toMap();
  const auto role_properties = role_schema.value(QStringLiteral("inputSchema")).toMap()
                                   .value(QStringLiteral("properties")).toMap();
  if (!mute_schema.value(QStringLiteral("ok")).toBool() ||
      mute_properties.value(QStringLiteral("muted")).toMap()
              .value(QStringLiteral("type")).toString() != QStringLiteral("boolean") ||
      !role_schema.value(QStringLiteral("ok")).toBool() ||
      !role_properties.value(QStringLiteral("audioRole")).toMap()
           .value(QStringLiteral("enum")).toStringList().contains(
               QStringLiteral("ambience"))) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the registered action schemas should expose typed mute and role state");
  }

  const auto muted = registry.invoke(
      mute_command,
      command_values(old_id, QStringLiteral("muted"), true,
                     QStringLiteral("expectedMuted"), false));
  QVariantMap old_state;
  QVariantMap new_state;
  if (!muted.value(QStringLiteral("ok")).toBool() ||
      !muted.value(QStringLiteral("changed")).toBool() ||
      track_row(controller, old_id, &old_state) < 0 ||
      track_row(controller, new_id, &new_state) < 0 ||
      !old_state.value(QStringLiteral("muted")).toBool() ||
      new_state.value(QStringLiteral("muted")).toBool() ||
      new_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music")) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("muting the reference track should leave the replacement music track unmuted");
  }

  const auto after_mute_hash = controller.deterministicDocumentHash();
  const auto idempotent_mute = registry.invoke(
      mute_command,
      command_values(old_id, QStringLiteral("muted"), true,
                     QStringLiteral("expectedMuted"), false));
  if (!idempotent_mute.value(QStringLiteral("ok")).toBool() ||
      !idempotent_mute.value(QStringLiteral("alreadySatisfied")).toBool() ||
      idempotent_mute.value(QStringLiteral("changed")).toBool() ||
      controller.deterministicDocumentHash() != after_mute_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("repeating the desired mute state should be an unchanged idempotent success");
  }

  const auto stale_mute = registry.invokeActionWithinMutation(
      mute_command,
      command_values(old_id, QStringLiteral("muted"), false,
                     QStringLiteral("expectedMuted"), false));
  const auto missing_track = registry.invokeActionWithinMutation(
      mute_command,
      command_values(QStringLiteral("missing-track"), QStringLiteral("muted"),
                     true, QStringLiteral("expectedMuted"), false));
  const auto video_rows = controller.snapshotTimelineTracks();
  QString video_id;
  for (const auto& value : video_rows) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("trackKind")).toString() ==
        QStringLiteral("video")) {
      video_id = row.value(QStringLiteral("trackId")).toString();
      break;
    }
  }
  const auto wrong_kind = registry.invokeActionWithinMutation(
      mute_command,
      command_values(video_id, QStringLiteral("muted"), true,
                     QStringLiteral("expectedMuted"), false));
  const auto locked_result = registry.invokeActionWithinMutation(
      mute_command,
      command_values(locked_id, QStringLiteral("muted"), true,
                     QStringLiteral("expectedMuted"), false));
  if (stale_mute.value(QStringLiteral("ok")).toBool() ||
      stale_mute.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("stale_track_state") ||
      missing_track.value(QStringLiteral("ok")).toBool() ||
      wrong_kind.value(QStringLiteral("ok")).toBool() ||
      locked_result.value(QStringLiteral("ok")).toBool() ||
      controller.deterministicDocumentHash() != after_mute_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("stale, missing, non-audio, and locked targets should reject without mutation");
  }

  const auto role_change = registry.invoke(
      role_command,
      command_values(old_id, QStringLiteral("audioRole"),
                     QStringLiteral("ambience"),
                     QStringLiteral("expectedAudioRole"),
                     QStringLiteral("music")));
  if (!role_change.value(QStringLiteral("ok")).toBool() ||
      !role_change.value(QStringLiteral("changed")).toBool() ||
      track_row(controller, old_id, &old_state) < 0 ||
      track_row(controller, new_id, &new_state) < 0 ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("ambience") ||
      old_state.value(QStringLiteral("audioBus")).toString() !=
          QStringLiteral("sfx") ||
      new_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music")) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("role updates should affect one stable track and select its role bus");
  }
  const auto after_role_hash = controller.deterministicDocumentHash();
  const auto idempotent_role = registry.invoke(
      role_command,
      command_values(old_id, QStringLiteral("audioRole"),
                     QStringLiteral("ambience"),
                     QStringLiteral("expectedAudioRole"),
                     QStringLiteral("music")));
  const auto stale_role = registry.invokeActionWithinMutation(
      role_command,
      command_values(old_id, QStringLiteral("audioRole"),
                     QStringLiteral("effects"),
                     QStringLiteral("expectedAudioRole"),
                     QStringLiteral("music")));
  const auto invalid_role = registry.invoke(
      role_command,
      command_values(old_id, QStringLiteral("audioRole"),
                     QStringLiteral("not-a-role"),
                     QStringLiteral("expectedAudioRole"),
                     QStringLiteral("ambience")));
  if (!idempotent_role.value(QStringLiteral("ok")).toBool() ||
      !idempotent_role.value(QStringLiteral("alreadySatisfied")).toBool() ||
      idempotent_role.value(QStringLiteral("changed")).toBool() ||
      stale_role.value(QStringLiteral("ok")).toBool() ||
      !invalid_role.value(QStringLiteral("errorCode")).toString().startsWith(
          QStringLiteral("command_admission_rejected")) ||
      controller.deterministicDocumentHash() != after_role_hash) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("role updates should be enum-typed, idempotent, and stale-state guarded");
  }

  bool mute_journaled = false;
  bool role_journaled = false;
  const auto journal_steps = tasks.journal()->report()
                                .value(QStringLiteral("steps")).toList();
  for (const auto& step_value : journal_steps) {
    const auto step = step_value.toMap();
    if (step.value(QStringLiteral("status")).toString() !=
        QStringLiteral("completed")) continue;
    const auto command = step.value(QStringLiteral("command")).toString();
    mute_journaled |= command == mute_command;
    role_journaled |= command == role_command;
  }
  if (!mute_journaled || !role_journaled) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("both successful per-track actions should have durable native task receipts");
  }

  controller.undo();
  if (track_row(controller, old_id, &old_state) < 0 ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music") ||
      !old_state.value(QStringLiteral("muted")).toBool()) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("one undo should restore only the last role change");
  }
  controller.redo();
  if (track_row(controller, old_id, &old_state) < 0 ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("ambience") ||
      !old_state.value(QStringLiteral("muted")).toBool()) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("one redo should reapply the role change without touching mute state");
  }

  const auto task_state_before_save = controller.agentTaskDocumentState();
  const auto task_state_hash_before_save = QString::fromLatin1(
      QCryptographicHash::hash(task_state_before_save, QCryptographicHash::Sha256).toHex());
  const auto task_save_copy = registry.invoke(
      QStringLiteral("timeline.action.saveTimeline"),
      {{QStringLiteral("outputPath"), task_output_path}});
  if (!task_save_copy.value(QStringLiteral("ok")).toBool() ||
      !task_save_copy.value(QStringLiteral("readbackVerified")).toBool() ||
      task_save_copy.value(QStringLiteral("outputPath")).toString() != task_output_path) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("the registered task-owned save-copy action should read back the edited Timeline");
  }
  const auto task_state_after_save = controller.agentTaskDocumentState();
  const auto task_state_hash_after_save = QString::fromLatin1(
      QCryptographicHash::hash(task_state_after_save, QCryptographicHash::Sha256).toHex());
  if (task_state_hash_after_save != task_state_hash_before_save ||
      !controller.dirty() || !QFileInfo::exists(task_output_path)) {
    std::cerr << "task save-copy changed live state or failed to retain unsaved Timeline: before="
              << task_state_hash_before_save.toStdString()
              << "; after=" << task_state_hash_after_save.toStdString() << '\n';
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("task save-copy should persist the edited Timeline without changing the unsaved live document");
  }
  TimelineDocumentController reopened;
  reopened.setEditAccessPolicy([] { return true; });
  if (!reopened.loadTimeline(task_output_path) ||
      track_row(reopened, old_id, &old_state) < 0 ||
      track_row(reopened, new_id, &new_state) < 0 ||
      !old_state.value(QStringLiteral("muted")).toBool() ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("ambience") ||
      new_state.value(QStringLiteral("muted")).toBool() ||
      new_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music")) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("per-track mute and role state should survive a native save/reopen");
  }

  controller.undo();
  controller.undo();
  if (track_row(controller, old_id, &old_state) < 0 ||
      old_state.value(QStringLiteral("muted")).toBool() ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music")) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("undoing both adapter changes should restore original audio state");
  }
  controller.redo();
  controller.redo();
  if (track_row(controller, old_id, &old_state) < 0 ||
      !old_state.value(QStringLiteral("muted")).toBool() ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("ambience")) {
    (void)tasks.revert();
    controller.releaseAgentTaskDocumentForAutomation();
    return fail("redo should restore mute and role as separate native undoable edits");
  }

  const auto reverted = tasks.revert();
  const auto task_report = tasks.journal()->report();
  const auto resources = task_report.value(QStringLiteral("resources")).toList();
  const auto resource = resources.isEmpty() ? QVariantMap{} : resources.first().toMap();
  const auto current_task_state_hash = QString::fromLatin1(
      QCryptographicHash::hash(controller.agentTaskDocumentState(), QCryptographicHash::Sha256).toHex());
  controller.releaseAgentTaskDocumentForAutomation();
  if (!reverted.value(QStringLiteral("ok")).toBool() ||
      QFileInfo::exists(task_output_path) ||
      current_task_state_hash != task_baseline_state_hash ||
      track_row(controller, old_id, &old_state) < 0 ||
      old_state.value(QStringLiteral("muted")).toBool() ||
      old_state.value(QStringLiteral("audioRole")).toString() !=
          QStringLiteral("music")) {
    std::cerr << "task revert receipt: "
              << QJsonDocument::fromVariant(reverted).toJson(QJsonDocument::Compact).toStdString()
              << "; beforeHash=" << resource.value(QStringLiteral("beforeHash")).toString().toStdString()
              << "; journalCurrentHash=" << resource.value(QStringLiteral("currentHash")).toString().toStdString()
              << "; actualTaskStateHash=" << current_task_state_hash.toStdString()
              << "; taskBaselineStateHash=" << task_baseline_state_hash.toStdString()
              << "; hashBeforeSave=" << task_state_hash_before_save.toStdString()
              << "; documentHash=" << controller.deterministicDocumentHash().toStdString()
              << "; restoredTrack=" << old_state.value(QStringLiteral("muted")).toBool()
              << "/" << old_state.value(QStringLiteral("audioRole")).toString().toStdString()
              << '\n';
    return fail("task revert should restore the exact task baseline after UI undo/redo checks");
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication application(argc, argv);
  if (!run()) return 1;
  std::cout << "Timeline stable-ID audio state actions passed\n";
  return 0;
}
