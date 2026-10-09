#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

#include <QtGlobal>

#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QEvent>
#include <QFutureWatcher>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QProcess>
#include <QPromise>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include "timeline_document_controller.h"
#include "timeline_asset_store.h"
#include "job_store.h"
#include "worker_queue_registry.h"
#include <QDir>

namespace photo_editor::desktop {
struct TimelineHistoryTestAccess final {
  static QJsonObject document(const TimelineDocumentController& controller) {
    return controller.documentObject();
  }

  static quint64 workingSessionGeneration(
      const TimelineDocumentController& controller) {
    return controller.working_session_generation_;
  }

  static quint64 documentEditRevision(
      const TimelineDocumentController& controller) {
    return controller.document_edit_revision_;
  }

  static QFuture<bool> queueJournalEntry(
      TimelineDocumentController& controller,
      const QString& reason,
      const QFuture<QJsonObject>& document) {
    return controller.submitEditJournalEntry(reason, document);
  }

  static QFutureWatcher<bool>* editJournalWatcher(
      TimelineDocumentController& controller) {
    return &controller.edit_journal_watcher_;
  }
};
}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::TimelineDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

class JournalFutureGate final {
 public:
  explicit JournalFutureGate(QJsonObject document) : document_(std::move(document)) {
    promise_.start();
  }

  ~JournalFutureGate() { release(); }

  [[nodiscard]] QFuture<QJsonObject> future() const { return promise_.future(); }

  void release() {
    if (released_) return;
    promise_.addResult(std::move(document_));
    promise_.finish();
    released_ = true;
  }

 private:
  QPromise<QJsonObject> promise_;
  QJsonObject document_;
  bool released_ = false;
};

qint64 current_resident_bytes() {
#if defined(Q_OS_MACOS)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  const auto result = task_info(
      mach_task_self(),
      MACH_TASK_BASIC_INFO,
      reinterpret_cast<task_info_t>(&info),
      &count);
  return result == KERN_SUCCESS ? static_cast<qint64>(info.resident_size) : -1;
#elif defined(Q_OS_LINUX)
  QFile statm(QStringLiteral("/proc/self/statm"));
  if (!statm.open(QIODevice::ReadOnly)) {
    return -1;
  }
  const auto fields = statm.readAll().simplified().split(' ');
  if (fields.size() < 2) {
    return -1;
  }
  bool ok = false;
  const auto pages = fields.at(1).toLongLong(&ok);
  return ok ? pages * static_cast<qint64>(sysconf(_SC_PAGESIZE)) : -1;
#elif defined(Q_OS_WIN)
  PROCESS_MEMORY_COUNTERS counters{};
  counters.cb = sizeof(counters);
  return GetProcessMemoryInfo(
             GetCurrentProcess(), &counters, static_cast<DWORD>(sizeof(counters)))
             ? static_cast<qint64>(counters.WorkingSetSize)
             : -1;
#else
  return -1;
#endif
}

qint64 percentile_95(QVector<qint64> samples) {
  if (samples.isEmpty()) {
    return 0;
  }
  std::sort(samples.begin(), samples.end());
  const auto index = qBound(
      0,
      static_cast<int>(std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1,
      static_cast<int>(samples.size()) - 1);
  return samples.at(index);
}

QString hash_object(const QJsonObject& object) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(object).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QJsonObject timeline_project_state(QJsonObject document) {
  // updatedAt reflects serialization time and is deliberately excluded by the
  // controller's durable timeline identity hash.
  document.remove(QStringLiteral("updatedAt"));
  return document;
}

QVector<QJsonObject> timeline_journal_documents(const QString& path) {
  QVector<QJsonObject> documents;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return documents;
  while (!file.atEnd()) {
    const auto line = file.readLine().trimmed();
    if (line.isEmpty()) continue;
    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(line, &parse_error);
    if (parse_error.error == QJsonParseError::NoError && parsed.isObject()) {
      const auto document = parsed.object().value(QStringLiteral("document")).toObject();
      if (!document.isEmpty()) documents.push_back(document);
    }
  }
  return documents;
}

QJsonObject timeline_recovery_candidate_document(const QVariantMap& candidate) {
  const auto kind = candidate.value(QStringLiteral("kind")).toString();
  const auto path = candidate.value(QStringLiteral("sourcePath")).toString();
  if (path.isEmpty()) return {};
  if (kind == QStringLiteral("autosave")) {
    QJsonParseError error{};
    const auto parsed = QJsonDocument::fromJson(read_file(path), &error);
    return error.error == QJsonParseError::NoError && parsed.isObject()
               ? parsed.object()
               : QJsonObject{};
  }
  if (kind != QStringLiteral("journal")) return {};
  const auto snapshots = timeline_journal_documents(path);
  const auto valid_entry_count = candidate.value(QStringLiteral("validEntryCount")).toInt();
  if (valid_entry_count <= 0 || valid_entry_count > snapshots.size()) return {};
  return snapshots.at(valid_entry_count - 1);
}

bool timeline_journal_contains_snapshot(
    const QString& path,
    const QJsonObject& expected_document) {
  for (const auto& document : timeline_journal_documents(path)) {
    if (timeline_project_state(document) ==
        timeline_project_state(expected_document)) return true;
  }
  return false;
}

bool timeline_journal_preserves_ordered_snapshots(
    const QString& path,
    const QJsonObject& earlier_document,
    const QJsonObject& later_document) {
  const auto documents = timeline_journal_documents(path);
  const auto earlier_state = timeline_project_state(earlier_document);
  const auto later_state = timeline_project_state(later_document);
  for (auto earlier_index = 0; earlier_index < documents.size(); ++earlier_index) {
    if (timeline_project_state(documents.at(earlier_index)) != earlier_state) continue;
    for (auto later_index = earlier_index + 1; later_index < documents.size(); ++later_index) {
      if (timeline_project_state(documents.at(later_index)) == later_state) return true;
    }
  }
  return false;
}

bool wait_for_journal_snapshot(
    TimelineDocumentController* controller,
    const QString& path,
    const QJsonObject& expected_document,
    int timeout_ms = 10000) {
  if (controller == nullptr) return false;

  auto* watcher =
      photo_editor::desktop::TimelineHistoryTestAccess::editJournalWatcher(*controller);
  auto acknowledged = false;
  const auto connection = QObject::connect(
      watcher,
      &QFutureWatcher<bool>::finished,
      controller,
      [&]() {
        acknowledged = !watcher->isCanceled() && watcher->result() &&
            timeline_journal_contains_snapshot(path, expected_document);
      });
  const auto has_durable_receipt = [&]() {
    return watcher->future().isValid() && watcher->isFinished() &&
        !watcher->isCanceled() && watcher->result() &&
        timeline_journal_contains_snapshot(path, expected_document);
  };
  acknowledged = has_durable_receipt();
  QElapsedTimer timeout;
  timeout.start();
  while (!acknowledged && timeout.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    acknowledged = has_durable_receipt();
  }
  QObject::disconnect(connection);
  return acknowledged;
}

bool write_autosave_snapshot(const QString& canonical_path, const QJsonObject& document) {
  const auto autosave_path = canonical_path + QStringLiteral(".autosave");
  QFile file(autosave_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(QJsonDocument(document).toJson(QJsonDocument::Indented)) <= 0) {
    return false;
  }
  file.close();
  return file.open(QIODevice::ReadWrite) &&
         file.setFileTime(
             QDateTime::currentDateTime().addSecs(2),
             QFileDevice::FileModificationTime);
}

bool run_process(const QString& program, const QStringList& arguments) {
  QProcess process;
  process.start(program, arguments);
  return process.waitForFinished(30000) &&
         process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool wait_for_media_analysis(
    TimelineDocumentController* controller,
    int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (controller != nullptr && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->mediaAnalysisBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_transcription(
    TimelineDocumentController* controller,
    int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  while (controller != nullptr && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!controller->transcriptionBusy()) {
      return true;
    }
  }
  return false;
}

bool wait_for_recovery_open(
    TimelineDocumentController* controller,
    int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (controller != nullptr && controller->recoveryOpenInProgress() &&
         timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return controller != nullptr && !controller->recoveryOpenInProgress();
}

bool wait_for_source_integrity(TimelineDocumentController* controller, int timeout_ms = 10000) {
  if (controller == nullptr) return false;
  QElapsedTimer timer;
  timer.start();
  const auto integrity_is_ready = [controller]() {
    const auto clip = controller->currentClip();
    return clip.value(QStringLiteral("sourceIntegrityStatus")).toString() ==
               QStringLiteral("verified") &&
           clip.value(QStringLiteral("sourceContentSha256")).toString().size() == 64;
  };
  while (!integrity_is_ready() && timer.elapsed() < timeout_ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  if (integrity_is_ready()) return true;
  const auto clip = controller->currentClip();
  std::cerr << "source-integrity fixture timed out: status="
            << clip.value(QStringLiteral("sourceIntegrityStatus")).toString().toStdString()
            << " hash_bytes="
            << clip.value(QStringLiteral("sourceContentSha256")).toString().size()
            << " file=" << clip.value(QStringLiteral("filePath")).toString().toStdString()
            << " controller_status=" << controller->status().toStdString() << '\n';
  return false;
}

bool wait_for_recovery_candidates(
    TimelineDocumentController* controller,
    int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (controller != nullptr && controller->recoveryCandidatesLoading() &&
         timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return controller != nullptr && !controller->recoveryCandidatesLoading();
}

bool wait_for_canceled_recovery_worker(
    TimelineDocumentController* controller,
    int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (controller != nullptr && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto* watcher = controller->findChild<QFutureWatcherBase*>(
        QStringLiteral("timelineRecoveryOpenWatcher"), Qt::FindDirectChildrenOnly);
    if (watcher == nullptr || !watcher->isRunning()) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      return true;
    }
  }
  return controller != nullptr &&
         controller->findChild<QFutureWatcherBase*>(
             QStringLiteral("timelineRecoveryOpenWatcher"), Qt::FindDirectChildrenOnly) == nullptr;
}

QString wait_for_media_hover_frame(
    TimelineDocumentController* controller,
    const QString& asset_id,
    const QString& file_path,
    double source_seconds,
    int timeout_ms = 10000) {
  if (controller == nullptr) {
    return {};
  }
  const auto cached =
      controller->requestMediaHoverFrame(asset_id, file_path, source_seconds);
  if (!cached.isEmpty()) {
    return cached;
  }
  QString frame_source;
  const auto connection = QObject::connect(
      controller, &TimelineDocumentController::mediaHoverFrameReady,
      controller,
      [&frame_source, &asset_id, source_seconds](
          const QString& ready_asset_id,
          double ready_seconds,
          const QString& ready_source) {
        if (ready_asset_id == asset_id &&
            std::abs(ready_seconds - source_seconds) < 0.002) {
          frame_source = ready_source;
        }
      });
  QElapsedTimer timer;
  timer.start();
  while (frame_source.isEmpty() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  QObject::disconnect(connection);
  return frame_source;
}

double clip_start_by_id(
    const TimelineDocumentController& controller,
    const QString& clip_id) {
  for (const auto& track_value : controller.sequenceTracks()) {
    for (const auto& clip_value :
         track_value.toMap().value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("clipId")).toString() == clip_id) {
        return clip.value(QStringLiteral("startSeconds")).toDouble();
      }
    }
  }
  return -1.0;
}

double clip_start_by_asset_id(
    const TimelineDocumentController& controller,
    const QString& asset_id) {
  for (const auto& track_value : controller.sequenceTracks()) {
    for (const auto& clip_value :
         track_value.toMap().value(QStringLiteral("clips")).toList()) {
      const auto clip = clip_value.toMap();
      if (clip.value(QStringLiteral("assetId")).toString() == asset_id) {
        return clip.value(QStringLiteral("startSeconds")).toDouble();
      }
    }
  }
  return -1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("recovery proof needs an isolated workspace");
  }
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv(
      "PHOTO_EDITOR_APP_DATA_ROOT",
      workspace.filePath(QStringLiteral("app-data")).toUtf8());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope,
      workspace.filePath(QStringLiteral("settings/user")));
  QSettings::setPath(
      QSettings::IniFormat, QSettings::SystemScope,
      workspace.filePath(QStringLiteral("settings/system")));
  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  const auto recovery_lifecycle_only =
      QCoreApplication::arguments().contains(QStringLiteral("--recovery-lifecycle-only"));

  const auto still_path = workspace.filePath(QStringLiteral("source.jpg"));
  QFile still_file(still_path);
  if (!still_file.open(QIODevice::WriteOnly) || still_file.write("fixture") <= 0) {
    return fail("recovery proof should create source media");
  }
  still_file.close();

  // Save is independent of publishing; an active render consumes immutable
  // bytes while later saves replace only the editable canonical project.
  {
    TimelineDocumentController target, pending;
    target.createTimeline(QStringLiteral("Save without render"));
    target.addClipFromGallery(QStringLiteral("photo:save"), still_path, QStringLiteral("Still"), 4.0, {});
    const auto initial_jobs = photo_editor::desktop::loadPersistedJobRecords().size();
    if (!target.saveTimeline() || photo_editor::desktop::loadPersistedJobRecords().size() != initial_jobs)
      return fail("Plain Save must not enqueue a gallery render");
    if (!target.saveTimeline(true)) return fail("Explicit publishing should enqueue a render");
    const auto jobs = photo_editor::desktop::loadPersistedJobRecords();
    const auto render = std::find_if(jobs.cbegin(), jobs.cend(), [&](const auto& job) {
      return job.asset_id == target.timelineAssetId() && job.job_kind == QStringLiteral("timeline_render");
    });
    if (render == jobs.cend() || render->source_path == target.savedDocumentPath())
      return fail("Gallery rendering must use an immutable snapshot");
    const auto render_source = render->source_path;
    const auto render_bytes = read_file(render_source);
    target.addSequenceMarker(QStringLiteral("Edit during render"));
    if (!target.flushEditJournal()) {
      return fail("pre-save journal writes should settle before the async captured-revision fixture");
    }
    JournalFutureGate save_checkpoint_gate(
        photo_editor::desktop::TimelineHistoryTestAccess::document(target));
    const auto gated_checkpoint = photo_editor::desktop::TimelineHistoryTestAccess::queueJournalEntry(
        target, QStringLiteral("save-gate"), save_checkpoint_gate.future());
    if (!gated_checkpoint.isValid()) {
      return fail("the deterministic save checkpoint gate should be queued");
    }
    auto save_completion_count = 0;
    QObject::connect(
        &target,
        &TimelineDocumentController::timelineSaveCompleted,
        &target,
        [&](bool, const QString&, const QString&) { ++save_completion_count; });
    if (!target.saveTimelineAsync()) return fail("Save should remain available with a queued render");
    target.addSequenceMarker(QStringLiteral("Edit during asynchronous save"));
    QElapsedTimer staged_canonical_timeout;
    staged_canonical_timeout.start();
    auto captured_canonical_staged = false;
    while (target.timelineSaveInProgress() && staged_canonical_timeout.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const auto staged_document = QJsonDocument::fromJson(
          read_file(target.savedDocumentPath())).object();
      const auto staged_markers = staged_document.value(QStringLiteral("markers")).toArray();
      captured_canonical_staged =
          staged_markers.size() == 1 &&
          staged_markers.first().toObject().value(QStringLiteral("label")).toString() ==
              QStringLiteral("Edit during render");
      if (captured_canonical_staged) break;
    }
    if (!captured_canonical_staged || !target.timelineSaveInProgress() ||
        save_completion_count != 0) {
      return fail(
          "canonical save should finish while checkpoint is gated, without acknowledging completion");
    }
    save_checkpoint_gate.release();
    QElapsedTimer save_timeout;
    save_timeout.start();
    while (target.timelineSaveInProgress() && save_timeout.elapsed() < 5000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (target.timelineSaveInProgress() || target.dirty() || save_completion_count != 1 ||
        read_file(render_source) != render_bytes ||
        read_file(target.savedDocumentPath()) == render_bytes)
      return fail("Saving newer edits must leave queued render bytes unchanged");
    if (target.sequenceMarkers().size() != 2) {
      return fail("the controller should retain both edits while the save completes");
    }
    QFile saved_target_file(target.savedDocumentPath());
    QFile target_journal_file(target.editJournalPath());
    if (!saved_target_file.open(QIODevice::ReadOnly) ||
        !target_journal_file.open(QIODevice::ReadOnly)) {
      return fail("async save and recovery journal should both remain readable");
    }
    auto saved_target_document = QJsonDocument::fromJson(saved_target_file.readAll()).object();
    saved_target_file.close();
    const auto persisted_markers = saved_target_document.value(QStringLiteral("markers")).toArray();
    QSet<QString> persisted_marker_labels;
    for (const auto& marker_value : persisted_markers) {
      persisted_marker_labels.insert(
          marker_value.toObject().value(QStringLiteral("label")).toString());
    }
    const QSet<QString> expected_marker_labels{
        QStringLiteral("Edit during render"),
        QStringLiteral("Edit during asynchronous save")};
    if (persisted_markers.size() != 2 || persisted_marker_labels != expected_marker_labels) {
      return fail("canonical save should persist both markers from the captured and newer revisions");
    }
    saved_target_document.remove(QStringLiteral("updatedAt"));
    auto latest_journal_document = QJsonObject{};
    auto captured_revision_journaled = false;
    while (!target_journal_file.atEnd()) {
      const auto entry_document = QJsonDocument::fromJson(
          target_journal_file.readLine().trimmed());
      if (!entry_document.isObject()) continue;
      const auto entry = entry_document.object();
      auto journal_document = entry.value(QStringLiteral("document")).toObject();
      if (journal_document.isEmpty()) continue;
      journal_document.remove(QStringLiteral("updatedAt"));
      latest_journal_document = journal_document;

      QSet<QString> journaled_marker_labels;
      const auto journaled_markers = journal_document.value(QStringLiteral("markers")).toArray();
      for (const auto& marker_value : journaled_markers) {
        journaled_marker_labels.insert(
            marker_value.toObject().value(QStringLiteral("label")).toString());
      }
      if (journaled_markers.size() == 1 &&
          journaled_marker_labels == QSet<QString>{QStringLiteral("Edit during render")}) {
        captured_revision_journaled = true;
      }
    }
    target_journal_file.close();
    if (!captured_revision_journaled || latest_journal_document.isEmpty() ||
        latest_journal_document != saved_target_document) {
      return fail(
          "the async save must journal its captured revision and finish with the exact canonical revision");
    }

    TimelineDocumentController failed_save;
    failed_save.createTimeline(QStringLiteral("Failed Canonical Save"));
    failed_save.addClipFromGallery(
        QStringLiteral("photo:failed-save"), still_path, QStringLiteral("Still"), 4.0, {});
    failed_save.addSequenceMarker(QStringLiteral("Latest durable save revision"));
    const auto documents_root = workspace.filePath(QStringLiteral("app-data/timelines/documents"));
    const auto timeline_id = failed_save.timelineAssetId().mid(QStringLiteral("timeline:").size());
    const auto blocked_canonical_path = QDir(documents_root).filePath(
        timeline_id + QStringLiteral(".json"));
    if (timeline_id.isEmpty() || !QDir().mkpath(blocked_canonical_path)) {
      return fail("failed-save fixture should block only the canonical document path");
    }
    JournalFutureGate failed_save_checkpoint_gate(
        photo_editor::desktop::TimelineHistoryTestAccess::document(failed_save));
    const auto failed_gate_checkpoint =
        photo_editor::desktop::TimelineHistoryTestAccess::queueJournalEntry(
            failed_save, QStringLiteral("save-failure-gate"),
            failed_save_checkpoint_gate.future());
    if (!failed_gate_checkpoint.isValid()) {
      return fail("the deterministic failed-save checkpoint gate should be queued");
    }
    auto failed_save_completion_count = 0;
    QObject::connect(
        &failed_save,
        &TimelineDocumentController::timelineSaveCompleted,
        &failed_save,
        [&](bool, const QString&, const QString&) { ++failed_save_completion_count; });
    if (!failed_save.saveTimelineAsync()) {
      return fail("failed-save fixture should start with its canonical path blocked");
    }
    QElapsedTimer failed_save_gate_timeout;
    failed_save_gate_timeout.start();
    while (failed_save.timelineSaveInProgress() &&
           failed_save_completion_count == 0 &&
           failed_save_gate_timeout.elapsed() < 150) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (!failed_save.timelineSaveInProgress() || failed_save_completion_count != 0) {
      return fail("failed canonical save must not complete before its checkpoint is durable");
    }
    failed_save_checkpoint_gate.release();
    QElapsedTimer failed_save_timeout;
    failed_save_timeout.start();
    while (failed_save.timelineSaveInProgress() && failed_save_timeout.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    bool failed_revision_recoverable = false;
    const auto failed_save_journal = failed_save.editJournalPath();
    for (const auto& candidate_value : failed_save.recoveryCandidates()) {
      const auto candidate = candidate_value.toMap();
      failed_revision_recoverable |=
          candidate.value(QStringLiteral("sourcePath")).toString() == failed_save_journal &&
          candidate.value(QStringLiteral("markerCount")).toInt() == 1;
    }
    const auto failed_save_was_reported =
        failed_save.status().contains(QStringLiteral("Unable to save the timeline document"));
    if (failed_save.timelineSaveInProgress() || failed_save_completion_count != 1 ||
        !failed_save.dirty() ||
        !failed_save_was_reported || !failed_revision_recoverable) {
      return fail("a failed canonical async save must leave its exact revision durably recoverable");
    }
    if (!QDir(blocked_canonical_path).removeRecursively()) {
      return fail("failed-save fixture should remove its test-owned canonical blocker");
    }

    pending.createTimeline(QStringLiteral("Pending replacement"));
    pending.addClipFromGallery(QStringLiteral("photo:pending"), still_path, QStringLiteral("Still"), 4.0, {});
    if (!pending.flushEditJournal()) return fail("Replacement fixture needs a validated checkpoint");
    const auto old_journal = pending.editJournalPath();
    pending.addSequenceMarker(QStringLiteral("Latest pending edit"));
    if (pending.loadTimeline(workspace.filePath(QStringLiteral("missing-replacement.json"))) ||
        pending.editJournalPath() != old_journal || pending.sequenceMarkers().size() != 1)
      return fail("An unavailable replacement must leave the active document and pending edits intact");
    if (!pending.loadTimeline(target.savedDocumentPath())) return fail("A valid replacement should load");
    bool newest_recoverable = false;
    for (const auto& value : pending.recoveryCandidates()) {
      const auto row = value.toMap();
      newest_recoverable |= row.value(QStringLiteral("sourcePath")) == old_journal &&
                            row.value(QStringLiteral("markerCount")).toInt() == 1;
    }
    if (!newest_recoverable) return fail("Replacement before the timer fires must preserve the newest edit");

    const auto markers_before_new = pending.sequenceMarkers().size();
    pending.addSequenceMarker(QStringLiteral("Pending before New"));
    const auto before_new_journal = pending.editJournalPath();
    pending.createTimeline(QStringLiteral("New project"));
    bool new_recoverable = false;
    for (const auto& value : pending.recoveryCandidates()) {
      const auto row = value.toMap();
      new_recoverable |= row.value(QStringLiteral("sourcePath")) == before_new_journal &&
                        row.value(QStringLiteral("markerCount")).toInt() == markers_before_new + 1;
    }
    if (!new_recoverable) return fail("New Timeline must preserve pending edits in the replaced project");

    pending.addClipFromGallery(QStringLiteral("photo:blocked"), still_path, QStringLiteral("Still"), 4.0, {});
    const auto blocked_journal = pending.editJournalPath();
    if (!QDir().mkpath(blocked_journal)) return fail("Checkpoint failure fixture should block the journal path");
    if (pending.loadTimeline(target.savedDocumentPath()) || pending.editJournalPath() != blocked_journal)
      return fail("Replacement must refuse to discard edits when checkpoint persistence fails");
    pending.closeTimeline();
    if (!pending.hasActiveTimeline()) return fail("Close must preserve the document after a checkpoint failure");
    if (!QDir().rmdir(blocked_journal)) return fail("Checkpoint failure fixture should restore writable storage");
    if (!pending.flushEditJournal()) return fail("A failed checkpoint must be retryable");

    QFile crash_tail(blocked_journal);
    if (!crash_tail.open(QIODevice::WriteOnly | QIODevice::Append) || crash_tail.write("{partial") <= 0)
      return fail("Cached-head corruption fixture should append a crash tail");
    crash_tail.close();
    const auto damaged_bytes = read_file(blocked_journal);
    pending.addSequenceMarker(QStringLiteral("After damaged tail"));
    if (!pending.flushEditJournal()) return fail("A changed journal must revalidate and preserve the damaged bytes");
    const auto archives = QDir(QFileInfo(blocked_journal).absolutePath()).entryInfoList(
        {QFileInfo(blocked_journal).fileName() + QStringLiteral(".damaged-*")}, QDir::Files);
    if (archives.size() != 1 || read_file(archives.first().absoluteFilePath()) != damaged_bytes)
      return fail("Checkpointing after a damaged tail must retain the original journal byte for byte");

    using photo_editor::desktop::WorkerQueueRegistry;
    pending.setPhotoWorkIntent(true, true, QStringLiteral("photo:focused"));
    pending.setBackgroundWorkIntent(false, false, {});
    if (!WorkerQueueRegistry::shouldDefer(QStringLiteral("proxy"), QStringLiteral("timeline"), {}) ||
        WorkerQueueRegistry::userIntentSnapshot().value(QStringLiteral("surface")) != QStringLiteral("edit"))
      return fail("Hidden Timeline callbacks must preserve foreground photo editing priority");
    pending.setPhotoWorkIntent(true, false, QStringLiteral("photo:focused"));
    if (WorkerQueueRegistry::shouldDefer(QStringLiteral("proxy"), QStringLiteral("timeline"), {}))
      return fail("Background work should resume after photo preview interaction finishes");
    pending.setPhotoWorkIntent(false, false, {});
  }

  // A save can finish after the user opens same-ID recoverable work. Its
  // canonical snapshot remains a legitimate saved version, but completion must
  // not mark the newly opened working session clean or chain-save it over that
  // canonical version. Save result rows also must retain unrelated recoveries.
  {
    TimelineDocumentController raced, unrelated;
    raced.createTimeline(QStringLiteral("Same-ID recovery save race"));
    raced.addClipFromGallery(
        QStringLiteral("photo:recovery-race"), still_path,
        QStringLiteral("Still"), 4.0, {});
    if (!wait_for_source_integrity(&raced))
      return fail("same-ID recovery fixture should settle its source fingerprint before capturing the exact saved revision");
    raced.addSequenceMarker(QStringLiteral("Recoverable state"));
    if (!raced.saveTimeline())
      return fail("same-ID recovery race should establish a saved base document");
    const auto recovered_document =
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced);
    raced.addSequenceMarker(QStringLiteral("Explicitly saved state"));
    if (!raced.saveTimeline() ||
        !write_autosave_snapshot(raced.savedDocumentPath(), recovered_document)) {
      return fail("same-ID recovery race should create a distinct older autosave");
    }

    unrelated.createTimeline(QStringLiteral("Unrelated recent recovery"));
    unrelated.addClipFromGallery(
        QStringLiteral("photo:unrelated-recovery"), still_path,
        QStringLiteral("Still"), 4.0, {});
    if (!wait_for_source_integrity(&unrelated))
      return fail("unrelated recovery fixture should settle its source fingerprint before capturing its exact saved revision");
    unrelated.addSequenceMarker(QStringLiteral("Unrelated recoverable state"));
    const auto unrelated_recovery =
        photo_editor::desktop::TimelineHistoryTestAccess::document(unrelated);
    unrelated.addSequenceMarker(QStringLiteral("Unrelated saved state"));
    if (!unrelated.saveTimeline() ||
        !write_autosave_snapshot(unrelated.savedDocumentPath(), unrelated_recovery)) {
      return fail("Recents preservation fixture should create an unrelated autosave recovery");
    }

    QString recovery_candidate_id;
    for (const auto& value : raced.recoveryCandidates()) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("sourcePath")).toString() ==
          raced.savedDocumentPath() + QStringLiteral(".autosave")) {
        recovery_candidate_id = candidate.value(QStringLiteral("candidateId")).toString();
        break;
      }
    }
    QString unrelated_candidate_id;
    for (const auto& value : raced.recoveryCandidates()) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("sourcePath")).toString() ==
          unrelated.savedDocumentPath() + QStringLiteral(".autosave")) {
        unrelated_candidate_id = candidate.value(QStringLiteral("candidateId")).toString();
        break;
      }
    }
    if (recovery_candidate_id.isEmpty() || unrelated_candidate_id.isEmpty()) {
      return fail("same-ID and unrelated autosave recoveries should be discoverable");
    }

    // Explicit candidate opens are keyed by durable candidate identity and do
    // not depend on which single recovery row Recents currently surfaces.
    const auto canonical_before_explicit_reopen = read_file(raced.savedDocumentPath());
    const auto stored_autosave_document = QJsonDocument::fromJson(
        read_file(raced.savedDocumentPath() + QStringLiteral(".autosave"))).object();
    TimelineDocumentController explicit_candidate_reopen;
    auto explicit_completion_count = 0;
    auto explicit_completion_succeeded = false;
    QObject::connect(
        &explicit_candidate_reopen,
        &TimelineDocumentController::recoveryOpenCompleted,
        &explicit_candidate_reopen,
        [&](const QString&, bool succeeded) {
          ++explicit_completion_count;
          explicit_completion_succeeded = succeeded;
        });
    const auto explicit_open_admitted =
        explicit_candidate_reopen.openRecoveryCandidate(recovery_candidate_id);
    const auto explicit_open_settled = explicit_open_admitted &&
        wait_for_recovery_open(&explicit_candidate_reopen);
    const auto explicitly_reopened_document =
        photo_editor::desktop::TimelineHistoryTestAccess::document(
            explicit_candidate_reopen);
    const auto explicit_canonical_bytes_preserved =
        read_file(raced.savedDocumentPath()) == canonical_before_explicit_reopen;
    if (!explicit_open_settled || explicit_completion_count != 1 ||
        !explicit_completion_succeeded || !explicit_candidate_reopen.dirty() ||
        explicit_candidate_reopen.sequenceMarkers().size() != 1 ||
        stored_autosave_document != recovered_document ||
        timeline_project_state(explicitly_reopened_document) !=
            timeline_project_state(recovered_document) ||
        explicitly_reopened_document.value(QStringLiteral("timelineId")) !=
            recovered_document.value(QStringLiteral("timelineId")) ||
        explicitly_reopened_document.value(QStringLiteral("createdAt")) !=
            recovered_document.value(QStringLiteral("createdAt")) ||
        !explicit_canonical_bytes_preserved) {
      const auto expected_document_hash = QCryptographicHash::hash(
          QJsonDocument(recovered_document).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256).toHex();
      const auto actual_document_hash = QCryptographicHash::hash(
          QJsonDocument(explicitly_reopened_document).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256).toHex();
      QSet<QString> snapshot_keys;
      for (const auto& key : recovered_document.keys()) snapshot_keys.insert(key);
      for (const auto& key : explicitly_reopened_document.keys()) snapshot_keys.insert(key);
      std::cerr << "explicit recovery candidate diagnostic: candidate="
                << recovery_candidate_id.toStdString()
                << " admitted=" << explicit_open_admitted
                << " settled=" << explicit_open_settled
                << " completion_count=" << explicit_completion_count
                << " completed_success=" << explicit_completion_succeeded
                << " status=" << explicit_candidate_reopen.status().toStdString()
                << " dirty=" << explicit_candidate_reopen.dirty()
                << " marker_count=" << explicit_candidate_reopen.sequenceMarkers().size()
                << " expected_document_sha256=" << expected_document_hash.toStdString()
                << " actual_document_sha256=" << actual_document_hash.toStdString()
                << " canonical_preserved=" << explicit_canonical_bytes_preserved << '\n';
      for (const auto& key : snapshot_keys) {
        if (recovered_document.value(key) != explicitly_reopened_document.value(key)) {
          const auto expected_value = QJsonDocument(QJsonObject{{key, recovered_document.value(key)}})
                                          .toJson(QJsonDocument::Compact);
          const auto actual_value = QJsonDocument(QJsonObject{{key, explicitly_reopened_document.value(key)}})
                                        .toJson(QJsonDocument::Compact);
          std::cerr << "  snapshot field differs: " << key.toStdString()
                    << " expected=" << expected_value.constData()
                    << " actual=" << actual_value.constData() << '\n';
        }
      }
      return fail("directly opening the explicit autosave candidate should restore its exact one-marker document without changing saved bytes");
    }

    raced.refreshRecentTimelines();
    QElapsedTimer recent_rows_timeout;
    recent_rows_timeout.start();
    while (raced.recentTimelinesLoading() && recent_rows_timeout.elapsed() < 5000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    const auto recent_rows_before_save = raced.recentTimelineRecords();
    const auto unrelated_row_before_save = std::any_of(
        recent_rows_before_save.cbegin(), recent_rows_before_save.cend(),
        [&unrelated_candidate_id](const QVariant& value) {
          return value.toMap().value(QStringLiteral("recoveryCandidateId")).toString() ==
                 unrelated_candidate_id;
        });
    if (!unrelated_row_before_save)
      return fail("Recents should contain the unrelated recovery before the save race");

    JournalFutureGate checkpoint_gate(
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced));
    const auto gated_checkpoint =
        photo_editor::desktop::TimelineHistoryTestAccess::queueJournalEntry(
            raced, QStringLiteral("same-id-save-race-gate"), checkpoint_gate.future());
    if (!gated_checkpoint.isValid() || !raced.saveTimelineAsync()) {
      return fail("same-ID save race should start behind a deterministic checkpoint gate");
    }

    QElapsedTimer canonical_commit_timeout;
    canonical_commit_timeout.start();
    while (canonical_commit_timeout.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const auto saved = QJsonDocument::fromJson(read_file(raced.savedDocumentPath())).object();
      if (saved.value(QStringLiteral("markers")).toArray().size() == 2) break;
    }
    const auto save_in_progress_before = raced.timelineSaveInProgress();
    const auto recovery_open_before = raced.recoveryOpenInProgress();
    const auto dirty_before_rejected_open = raced.dirty();
    const auto markers_before_rejected_open = raced.sequenceMarkers();
    const auto document_before_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced);
    const auto document_state_before_rejected_open =
        timeline_project_state(document_before_rejected_open);
    const auto session_before_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::workingSessionGeneration(raced);
    const auto revision_before_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(raced);
    const auto canonical_bytes_before_rejected_open =
        read_file(raced.savedDocumentPath());

    const auto recovery_open_accepted =
        raced.openRecoveryCandidate(recovery_candidate_id);
    const auto save_in_progress_after = raced.timelineSaveInProgress();
    const auto recovery_open_after = raced.recoveryOpenInProgress();
    const auto dirty_after_rejected_open = raced.dirty();
    const auto markers_after_rejected_open = raced.sequenceMarkers();
    const auto document_after_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced);
    const auto document_state_after_rejected_open =
        timeline_project_state(document_after_rejected_open);
    const auto session_after_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::workingSessionGeneration(raced);
    const auto revision_after_rejected_open =
        photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(raced);
    const auto canonical_bytes_after_rejected_open =
        read_file(raced.savedDocumentPath());
    const auto rejected_open_left_state_unchanged =
        !recovery_open_accepted && !recovery_open_before && !recovery_open_after &&
        dirty_after_rejected_open == dirty_before_rejected_open &&
        markers_after_rejected_open == markers_before_rejected_open &&
        document_state_after_rejected_open == document_state_before_rejected_open &&
        session_after_rejected_open == session_before_rejected_open &&
        revision_after_rejected_open == revision_before_rejected_open &&
        canonical_bytes_after_rejected_open == canonical_bytes_before_rejected_open;
    if (!save_in_progress_before || !save_in_progress_after ||
        markers_before_rejected_open.size() != 2 ||
        !rejected_open_left_state_unchanged) {
      std::cerr << "same-ID Save/recovery rejection predicates: save_before="
                << save_in_progress_before << " save_after=" << save_in_progress_after
                << " open_accepted=" << recovery_open_accepted
                << " recovery_open_before=" << recovery_open_before
                << " recovery_open_after=" << recovery_open_after
                << " dirty_before=" << dirty_before_rejected_open
                << " dirty_after=" << dirty_after_rejected_open
                << " markers_before=" << markers_before_rejected_open.size()
                << " markers_after=" << markers_after_rejected_open.size()
                << " doc_hash_before="
                << hash_object(document_state_before_rejected_open).toStdString()
                << " doc_hash_after="
                << hash_object(document_state_after_rejected_open).toStdString()
                << " session_before=" << session_before_rejected_open
                << " session_after=" << session_after_rejected_open
                << " revision_before=" << revision_before_rejected_open
                << " revision_after=" << revision_after_rejected_open
                << " canonical_bytes_unchanged="
                << (canonical_bytes_after_rejected_open ==
                    canonical_bytes_before_rejected_open)
                << " status=" << raced.status().toStdString() << '\n';
      checkpoint_gate.release();
      return fail("recovery must refuse to open while an async Save is mutating canonical state");
    }
    checkpoint_gate.release();
    QElapsedTimer save_completion_timeout;
    save_completion_timeout.start();
    while (raced.timelineSaveInProgress() && save_completion_timeout.elapsed() < 10000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QElapsedTimer recent_refresh_timeout;
    recent_refresh_timeout.start();
    while (raced.recentTimelinesLoading() && recent_refresh_timeout.elapsed() < 10000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    if (raced.timelineSaveInProgress() || raced.dirty() ||
        !raced.openRecoveryCandidate(recovery_candidate_id) ||
        !raced.recoveryOpenInProgress() || raced.saveTimelineAsync() ||
        !raced.recoveryOpenInProgress() || !wait_for_recovery_open(&raced) ||
        !raced.dirty() || raced.sequenceMarkers().size() != 1 ||
        timeline_project_state(photo_editor::desktop::TimelineHistoryTestAccess::document(raced)) !=
            timeline_project_state(recovered_document)) {
      return fail("the verified same-ID recovery should open as dirty work after Save completes");
    }
    recent_refresh_timeout.restart();
    while (raced.recentTimelinesLoading() && recent_refresh_timeout.elapsed() < 10000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    const auto canonical_after_save =
        QJsonDocument::fromJson(read_file(raced.savedDocumentPath())).object();
    const auto canonical_markers = canonical_after_save.value(QStringLiteral("markers")).toArray();
    const auto marker_labels = [&canonical_markers]() {
      QSet<QString> labels;
      for (const auto& marker : canonical_markers)
        labels.insert(marker.toObject().value(QStringLiteral("label")).toString());
      return labels;
    }();
    const auto recent_rows_after_save = raced.recentTimelineRecords();
    const auto unrelated_row_after_save = std::any_of(
        recent_rows_after_save.cbegin(), recent_rows_after_save.cend(),
        [&unrelated_candidate_id](const QVariant& value) {
          return value.toMap().value(QStringLiteral("recoveryCandidateId")).toString() ==
                 unrelated_candidate_id;
        });
    if (raced.timelineSaveInProgress() || !raced.dirty() ||
        raced.sequenceMarkers().size() != 1 || canonical_markers.size() != 2 ||
        !marker_labels.contains(QStringLiteral("Recoverable state")) ||
        !marker_labels.contains(QStringLiteral("Explicitly saved state")) ||
        !unrelated_row_after_save) {
      return fail("stale save completion must preserve the current recovery, canonical bytes, and unrelated Recents rows");
    }

    const auto canonical_bytes_after_save = read_file(raced.savedDocumentPath());
    raced.addSequenceMarker(QStringLiteral("Unsaved current session before recovery"));
    const auto dirty_checkpoint_document =
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced);
    if (!raced.flushEditJournal())
      return fail("the dirty-session replacement fixture should have a durable current snapshot");
    JournalFutureGate recovery_checkpoint_gate(
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced));
    const auto recovery_checkpoint_gate_future =
        photo_editor::desktop::TimelineHistoryTestAccess::queueJournalEntry(
            raced, QStringLiteral("recovery-checkpoint-gate"),
            recovery_checkpoint_gate.future());
    if (!recovery_checkpoint_gate_future.isValid() ||
        !raced.openRecoveryCandidate(recovery_candidate_id)) {
      return fail("recovery replacement should begin its async read while the current dirty checkpoint is gated");
    }
    QElapsedTimer checkpoint_status_timeout;
    checkpoint_status_timeout.start();
    while (!raced.status().contains(QStringLiteral("checkpoint before opening")) &&
           raced.recoveryOpenInProgress() && checkpoint_status_timeout.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if (!raced.recoveryOpenInProgress() ||
        !raced.status().contains(QStringLiteral("checkpoint before opening"))) {
      recovery_checkpoint_gate.release();
      return fail("recovery replacement should expose its pending durable checkpoint state");
    }
    auto heartbeat_count = 0;
    QTimer event_loop_heartbeat;
    event_loop_heartbeat.setInterval(5);
    QObject::connect(&event_loop_heartbeat, &QTimer::timeout, [&heartbeat_count]() {
      ++heartbeat_count;
    });
    event_loop_heartbeat.start();
    QElapsedTimer heartbeat_duration;
    heartbeat_duration.start();
    while (heartbeat_duration.elapsed() < 100)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    event_loop_heartbeat.stop();
    if (heartbeat_count < 5 || !raced.recoveryOpenInProgress() ||
        !raced.dirty() || raced.sequenceMarkers().size() != 2 ||
        read_file(raced.savedDocumentPath()) != canonical_bytes_after_save) {
      recovery_checkpoint_gate.release();
      return fail("a gated dirty-session checkpoint should leave the event loop, current work, and saved bytes responsive");
    }
    recovery_checkpoint_gate.release();
    if (!wait_for_recovery_open(&raced) || !raced.dirty() ||
        raced.sequenceMarkers().size() != 1 ||
        read_file(raced.savedDocumentPath()) != canonical_bytes_after_save) {
      return fail("the verified recovery should replace dirty work only after its current-session checkpoint commits");
    }
    const auto applied_recovery_document =
        photo_editor::desktop::TimelineHistoryTestAccess::document(raced);
    const auto journal_path_after_recovery = raced.editJournalPath();
    if (!timeline_journal_contains_snapshot(
            journal_path_after_recovery, dirty_checkpoint_document)) {
      return fail("the replaced two-marker working snapshot must remain in the append-only journal history");
    }

    auto* recovery_state_journal_watcher =
        photo_editor::desktop::TimelineHistoryTestAccess::editJournalWatcher(raced);
    const auto recovery_checkpoint_is_durable_and_ordered = [&]() {
      if (!recovery_state_journal_watcher->future().isValid() ||
          !recovery_state_journal_watcher->isFinished() ||
          recovery_state_journal_watcher->isCanceled() ||
          !recovery_state_journal_watcher->result()) {
        return false;
      }
      return timeline_journal_preserves_ordered_snapshots(
          journal_path_after_recovery,
          dirty_checkpoint_document,
          recovered_document);
    };
    auto recovery_state_checkpoint_acknowledged = false;
    const auto journal_ack_connection = QObject::connect(
        recovery_state_journal_watcher,
        &QFutureWatcher<bool>::finished,
        &raced,
        [&]() {
          recovery_state_checkpoint_acknowledged =
              recovery_checkpoint_is_durable_and_ordered();
        });
    // The autosave timer may have completed the journal write before this
    // observer was connected. Consume an already-finished acknowledgement too.
    recovery_state_checkpoint_acknowledged =
        recovery_checkpoint_is_durable_and_ordered();
    QElapsedTimer recovery_state_checkpoint_timeout;
    recovery_state_checkpoint_timeout.start();
    while (!recovery_state_checkpoint_acknowledged &&
           recovery_state_checkpoint_timeout.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QObject::disconnect(journal_ack_connection);
    if (!recovery_state_checkpoint_acknowledged ||
        !timeline_journal_preserves_ordered_snapshots(
            journal_path_after_recovery,
            dirty_checkpoint_document,
            recovered_document)) {
      const auto raw_snapshots = timeline_journal_documents(journal_path_after_recovery);
      const auto has_replaced_snapshot = timeline_journal_contains_snapshot(
          journal_path_after_recovery, dirty_checkpoint_document);
      const auto ordered_snapshots = timeline_journal_preserves_ordered_snapshots(
          journal_path_after_recovery, dirty_checkpoint_document, recovered_document);
      const auto journal_bytes = read_file(journal_path_after_recovery);
      const auto normalized_expected_recovery = timeline_project_state(recovered_document);
      const auto normalized_applied_recovery =
          timeline_project_state(applied_recovery_document);
      QSet<QString> expected_recovery_keys;
      for (const auto& key : normalized_expected_recovery.keys())
        expected_recovery_keys.insert(key);
      for (const auto& key : normalized_applied_recovery.keys())
        expected_recovery_keys.insert(key);
      QStringList applied_recovery_difference_keys;
      for (const auto& key : expected_recovery_keys) {
        if (normalized_expected_recovery.value(key) != normalized_applied_recovery.value(key))
          applied_recovery_difference_keys.push_back(key);
      }
      if (qEnvironmentVariableIsSet("LOOKSY_KEEP_TIMELINE_TEST_WORKSPACE")) {
        workspace.setAutoRemove(false);
        std::cerr << "  preserved recovery diagnostic workspace="
                  << workspace.path().toStdString() << '\n';
      }
      std::cerr << "recovery checkpoint acknowledgement diagnostic: future_valid="
                << recovery_state_journal_watcher->future().isValid()
                << " watcher_running=" << recovery_state_journal_watcher->isRunning()
                << " watcher_finished=" << recovery_state_journal_watcher->isFinished()
                << " watcher_canceled=" << recovery_state_journal_watcher->isCanceled()
                << " watcher_result="
                << (recovery_state_journal_watcher->isFinished() &&
                            !recovery_state_journal_watcher->isCanceled()
                        ? recovery_state_journal_watcher->result()
                        : false)
                << " raw_snapshot_count=" << raw_snapshots.size()
                << " preserved_two_marker_snapshot=" << has_replaced_snapshot
                << " ordered_two_to_one_marker_snapshots=" << ordered_snapshots
                << " dirty_checkpoint_markers="
                << dirty_checkpoint_document.value(QStringLiteral("markers")).toArray().size()
                << " recovered_markers="
                << recovered_document.value(QStringLiteral("markers")).toArray().size()
                << " applied_matches_expected="
                << (normalized_applied_recovery == normalized_expected_recovery)
                << " applied_state_sha256=" << hash_object(normalized_applied_recovery).toStdString()
                << " expected_state_sha256=" << hash_object(normalized_expected_recovery).toStdString()
                << " applied_difference_keys="
                << applied_recovery_difference_keys.join(QLatin1Char(',')).toStdString()
                << " journal_bytes=" << journal_bytes.size()
                << " journal_sha256="
                << QString::fromLatin1(QCryptographicHash::hash(
                       journal_bytes, QCryptographicHash::Sha256).toHex()).toStdString()
                << '\n';
      for (auto index = 0; index < raw_snapshots.size(); ++index) {
        const auto normalized_snapshot = timeline_project_state(raw_snapshots.at(index));
        QSet<QString> snapshot_keys;
        for (const auto& key : normalized_snapshot.keys()) snapshot_keys.insert(key);
        for (const auto& key : normalized_expected_recovery.keys()) snapshot_keys.insert(key);
        QStringList difference_keys;
        for (const auto& key : snapshot_keys) {
          if (normalized_snapshot.value(key) != normalized_expected_recovery.value(key))
            difference_keys.push_back(key);
        }
        std::cerr << "  journal_snapshot[" << index << "] markers="
                  << normalized_snapshot.value(QStringLiteral("markers")).toArray().size()
                  << " state_sha256=" << hash_object(normalized_snapshot).toStdString()
                  << " matches_recovered="
                  << (normalized_snapshot == normalized_expected_recovery)
                  << " differs_from_recovered="
                  << difference_keys.join(QLatin1Char(',')).toStdString() << '\n';
      }
      return fail("the selected recovered working state should checkpoint after, and preserve, the replaced dirty snapshot");
    }

    TimelineDocumentController canceled_same_id_open;
    if (!canceled_same_id_open.loadTimeline(raced.savedDocumentPath()))
      return fail("the cancellation fixture should load the exact saved project first");
    const auto same_timeline_id = canceled_same_id_open.timelineAssetId();
    if (!canceled_same_id_open.openRecoveryCandidate(recovery_candidate_id) ||
        !canceled_same_id_open.recoveryOpenInProgress()) {
      return fail("same-ID recovery should enter pending state before cancellation");
    }
    canceled_same_id_open.cancelRecoveryOpen();
    const auto canceled_reload_succeeded =
        canceled_same_id_open.loadTimeline(raced.savedDocumentPath());
    const auto recovery_open_cleared_after_reload =
        !canceled_same_id_open.recoveryOpenInProgress();
    const auto canceled_worker_settled =
        wait_for_canceled_recovery_worker(&canceled_same_id_open);
    const auto canceled_reload_document =
        photo_editor::desktop::TimelineHistoryTestAccess::document(
            canceled_same_id_open);
    const auto canceled_reload_clip = canceled_same_id_open.currentClip();
    const auto canceled_reload_canonical_preserved =
        read_file(raced.savedDocumentPath()) == canonical_bytes_after_save;
    if (!canceled_reload_succeeded || !recovery_open_cleared_after_reload ||
        !canceled_worker_settled ||
        canceled_same_id_open.timelineAssetId() != same_timeline_id ||
        canceled_same_id_open.dirty() ||
        canceled_same_id_open.sequenceMarkers().size() != 2 ||
        !canceled_reload_canonical_preserved) {
      if (qEnvironmentVariableIsSet("LOOKSY_KEEP_TIMELINE_TEST_WORKSPACE")) {
        workspace.setAutoRemove(false);
        std::cerr << "  preserved recovery diagnostic workspace="
                  << workspace.path().toStdString() << '\n';
      }
      std::cerr << "canceled recovery reload diagnostic: reload_succeeded="
                << canceled_reload_succeeded
                << " open_cleared=" << recovery_open_cleared_after_reload
                << " worker_settled=" << canceled_worker_settled
                << " expected_timeline_id=" << same_timeline_id.toStdString()
                << " actual_timeline_id="
                << canceled_same_id_open.timelineAssetId().toStdString()
                << " dirty=" << canceled_same_id_open.dirty()
                << " marker_count=" << canceled_same_id_open.sequenceMarkers().size()
                << " document_state_sha256="
                << hash_object(timeline_project_state(canceled_reload_document)).toStdString()
                << " source_integrity_status="
                << canceled_reload_clip.value(QStringLiteral("sourceIntegrityStatus")).toString().toStdString()
                << " source_hash_bytes="
                << canceled_reload_clip.value(QStringLiteral("sourceContentSha256")).toString().size()
                << " canonical_bytes_preserved=" << canceled_reload_canonical_preserved
                << " status=" << canceled_same_id_open.status().toStdString() << '\n';
      return fail("a canceled same-ID open must not replace the reloaded saved generation or dirty canonical state");
    }

    TimelineDocumentController reopened_recovery;
    reopened_recovery.refreshRecentTimelines();
    QElapsedTimer reopen_recent_timeout;
    reopen_recent_timeout.start();
    while (reopened_recovery.recentTimelinesLoading() &&
           reopen_recent_timeout.elapsed() < 5000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const auto reopened_recent_rows = reopened_recovery.recentTimelineRecords();
    // The surfaced identity is intentionally read from the current Recent row:
    // journal IDs include the entry hash and may change as autosave/checkpoint
    // entries are appended or deduplicated. The fixed autosave candidate above
    // already proves direct-open identity against its exact stored snapshot.
    QVariantMap surfaced_recovery;
    for (const auto& row : reopened_recent_rows) {
      const auto record = row.toMap();
      if (record.value(QStringLiteral("timelineId")).toString() ==
              raced.timelineAssetId().mid(QStringLiteral("timeline:").size()) &&
          record.value(QStringLiteral("isRecovery")).toBool()) {
        surfaced_recovery = record;
        break;
      }
    }
    auto reopen_completion_count = 0;
    auto reopen_completed_successfully = false;
    QString reopen_completed_candidate_id;
    QObject::connect(
        &reopened_recovery,
        &TimelineDocumentController::recoveryOpenCompleted,
        &reopened_recovery,
        [&](const QString& candidate_id, bool succeeded) {
          ++reopen_completion_count;
          reopen_completed_candidate_id = candidate_id;
          reopen_completed_successfully = succeeded;
        });
    QElapsedTimer exact_reopen_timer;
    exact_reopen_timer.start();
    const auto recent_candidate_id =
        surfaced_recovery.value(QStringLiteral("recoveryCandidateId")).toString();
    QVariantMap surfaced_candidate_metadata;
    for (const auto& candidate_value : reopened_recovery.recoveryCandidates()) {
      const auto candidate = candidate_value.toMap();
      if (candidate.value(QStringLiteral("candidateId")).toString() ==
          recent_candidate_id) {
        surfaced_candidate_metadata = candidate;
      }
    }
    const auto surfaced_candidate_document =
        timeline_recovery_candidate_document(surfaced_candidate_metadata);
    const auto surfaced_candidate_state =
        timeline_project_state(surfaced_candidate_document);
    const auto expected_recovered_state = timeline_project_state(recovered_document);
    const auto surfaced_candidate_kind =
        surfaced_candidate_metadata.value(QStringLiteral("kind")).toString();
    const auto surfaced_candidate_path =
        surfaced_candidate_metadata.value(QStringLiteral("sourcePath")).toString();
    const auto surfaced_candidate_source_is_supported =
        (surfaced_candidate_kind == QStringLiteral("autosave") &&
         surfaced_candidate_path == raced.savedDocumentPath() + QStringLiteral(".autosave")) ||
        (surfaced_candidate_kind == QStringLiteral("journal") &&
         surfaced_candidate_path == journal_path_after_recovery);
    const auto surfaced_candidate_matches_expected =
        !surfaced_candidate_state.isEmpty() &&
        surfaced_candidate_state == expected_recovered_state;
    const auto reopen_admitted = !recent_candidate_id.isEmpty() &&
        reopened_recovery.openRecoveryCandidate(recent_candidate_id);
    const auto reopen_admission_ms = exact_reopen_timer.nsecsElapsed() / 1000000.0;
    const auto reopen_settled = reopen_admitted && wait_for_recovery_open(&reopened_recovery);
    const auto reopen_elapsed_ms = exact_reopen_timer.nsecsElapsed() / 1000000.0;
    const auto reopened_markers = reopened_recovery.sequenceMarkers();
    QSet<QString> reopened_marker_labels;
    for (const auto& marker : reopened_markers)
      reopened_marker_labels.insert(marker.toMap().value(QStringLiteral("label")).toString());
    const auto canonical_bytes_preserved =
        read_file(raced.savedDocumentPath()) == canonical_bytes_after_save;
    const auto raw_journal_documents =
        timeline_journal_documents(journal_path_after_recovery);
    // A later canceled reload can append the saved canonical state again to
    // this same-ID journal. Check that recovery appended its exact working
    // snapshot after the replaced dirty snapshot, while the surfaced Recent
    // candidate below proves which state is currently selected for reopen.
    const auto journal_history_preserved =
        timeline_journal_preserves_ordered_snapshots(
            journal_path_after_recovery,
            dirty_checkpoint_document,
            recovered_document) &&
        !raw_journal_documents.isEmpty();
    const auto reopened_recovery_state = timeline_project_state(
        photo_editor::desktop::TimelineHistoryTestAccess::document(reopened_recovery));
    const auto surfaced_recent_snapshot_exact =
        surfaced_candidate_source_is_supported && surfaced_candidate_matches_expected &&
        surfaced_candidate_metadata.value(QStringLiteral("markerCount")).toInt() ==
            surfaced_candidate_state.value(QStringLiteral("markers")).toArray().size() &&
        reopened_recovery_state == surfaced_candidate_state;
    const auto exact_recent_reopen =
        !surfaced_recovery.isEmpty() &&
        surfaced_candidate_metadata.value(QStringLiteral("candidateId")).toString() ==
            recent_candidate_id &&
        surfaced_recent_snapshot_exact &&
        reopen_settled && reopen_completion_count == 1 &&
        reopen_completed_successfully &&
        reopen_completed_candidate_id == recent_candidate_id &&
        reopened_recovery.dirty() && reopened_markers.size() == 1 &&
        reopened_marker_labels == QSet<QString>{QStringLiteral("Recoverable state")} &&
        reopened_recovery_state == expected_recovered_state &&
        reopened_recovery.timelineAssetId() == raced.timelineAssetId() &&
        photo_editor::desktop::TimelineHistoryTestAccess::document(
            reopened_recovery).value(QStringLiteral("createdAt")) ==
            recovered_document.value(QStringLiteral("createdAt")) &&
        canonical_bytes_preserved && journal_history_preserved;
    if (!exact_recent_reopen) {
      if (qEnvironmentVariableIsSet("LOOKSY_KEEP_TIMELINE_TEST_WORKSPACE")) {
        workspace.setAutoRemove(false);
        std::cerr << "  preserved recovery diagnostic workspace="
                  << workspace.path().toStdString() << '\n';
      }
      const auto canonical_hash = QCryptographicHash::hash(
          read_file(raced.savedDocumentPath()), QCryptographicHash::Sha256).toHex();
      const auto expected_canonical_hash = QCryptographicHash::hash(
          canonical_bytes_after_save, QCryptographicHash::Sha256).toHex();
      std::cerr << "recovery reopen diagnostic: surfaced_candidate="
                << recent_candidate_id.toStdString()
                << " recovery_kind="
                << surfaced_candidate_metadata.value(QStringLiteral("kind")).toString().toStdString()
                << " recovery_source="
                << surfaced_candidate_path.toStdString()
                << " recovery_marker_count="
                << surfaced_candidate_metadata.value(QStringLiteral("markerCount")).toInt()
                << " surfaced_snapshot_exact=" << surfaced_recent_snapshot_exact
                << " surfaced_state_sha256=" << hash_object(surfaced_candidate_state).toStdString()
                << " expected_state_sha256=" << hash_object(expected_recovered_state).toStdString()
                << " raw_journal_documents=" << raw_journal_documents.size()
                << " journal_latest_matches_expected="
                << (!raw_journal_documents.isEmpty() &&
                    timeline_project_state(raw_journal_documents.last()) == expected_recovered_state)
                << " journal_latest_sha256="
                << (raw_journal_documents.isEmpty()
                        ? QString{} : hash_object(timeline_project_state(raw_journal_documents.last())))
                       .toStdString()
                << " preserved_two_marker_checkpoint="
                << timeline_journal_contains_snapshot(
                       journal_path_after_recovery, dirty_checkpoint_document)
                << " ordered_checkpoint_history=" << journal_history_preserved
                << " expected_recent_snapshot_exact=" << surfaced_candidate_matches_expected
                << " admitted=" << reopen_admitted
                << " settled=" << reopen_settled
                << " completion_count=" << reopen_completion_count
                << " completed_candidate=" << reopen_completed_candidate_id.toStdString()
                << " completed_success=" << reopen_completed_successfully
                << " status=" << reopened_recovery.status().toStdString()
                << " dirty=" << reopened_recovery.dirty()
                << " marker_count=" << reopened_markers.size()
                << " elapsed_ms=" << reopen_elapsed_ms
                << " admission_ms=" << reopen_admission_ms
                << " canonical_sha256=" << canonical_hash.toStdString()
                << " expected_sha256=" << expected_canonical_hash.toStdString()
                << " canonical_preserved=" << canonical_bytes_preserved << '\n';
      return fail("the recovered state should reopen exactly while preserving the explicit saved project bytes");
    }

    TimelineDocumentController checkpoint_failure;
    checkpoint_failure.createTimeline(QStringLiteral("Recovery checkpoint failure"));
    checkpoint_failure.addClipFromGallery(
        QStringLiteral("photo:checkpoint-failure"), still_path,
        QStringLiteral("Still"), 4.0, {});
    checkpoint_failure.addSequenceMarker(QStringLiteral("Keep this dirty work open"));
    const auto checkpoint_failure_timeline_id = checkpoint_failure.timelineAssetId();
    if (!QDir().mkpath(checkpoint_failure.editJournalPath()) ||
        !checkpoint_failure.openRecoveryCandidate(unrelated_candidate_id) ||
        !wait_for_recovery_open(&checkpoint_failure) ||
        !checkpoint_failure.hasActiveTimeline() || !checkpoint_failure.dirty() ||
        checkpoint_failure.timelineAssetId() != checkpoint_failure_timeline_id ||
        checkpoint_failure.sequenceMarkers().size() != 1 ||
        !checkpoint_failure.status().contains(QStringLiteral("Your edits remain open"))) {
      return fail("a failed async pre-replacement checkpoint must keep the current dirty timeline intact");
    }
  }

  // Match the archived 128-clip / 40-checkpoint controller probe so the
  // foreground API latency can be compared with its synchronous baseline.
  {
    TimelineDocumentController large_recovery;
    large_recovery.createTimeline(QStringLiteral("Recovery latency fixture"));
    for (auto index = 0; index < 128; ++index) {
      large_recovery.addClipFromGallery(
          QStringLiteral("photo:recovery-latency-%1").arg(index), still_path,
          QStringLiteral("Still"), 4.0, {});
    }
    for (auto index = 0; index < 40; ++index) {
      large_recovery.addSequenceMarker(QStringLiteral("Checkpoint %1").arg(index));
      if (!large_recovery.flushEditJournal())
        return fail("the matched recovery latency fixture should persist all 40 checkpoints");
    }

    QElapsedTimer timer;
    timer.start();
    large_recovery.refreshRecoveryCandidates();
    const auto discovery_admission_ms = timer.nsecsElapsed() / 1000000.0;
    if (!wait_for_recovery_candidates(&large_recovery))
      return fail("asynchronous recovery discovery should complete for the matched fixture");
    const auto recovery_rows =
        large_recovery.property("recoveryCandidates").toList();
    QString large_candidate_id;
    for (const auto& value : recovery_rows) {
      const auto row = value.toMap();
      if (row.value(QStringLiteral("sourcePath")).toString() ==
          large_recovery.editJournalPath()) {
        large_candidate_id = row.value(QStringLiteral("candidateId")).toString();
        break;
      }
    }
    if (large_candidate_id.isEmpty())
      return fail("the async recovery list should expose the large journal candidate");

    TimelineDocumentController opening;
    timer.restart();
    const auto admitted = opening.openRecoveryCandidate(large_candidate_id);
    const auto open_admission_ms = timer.nsecsElapsed() / 1000000.0;
    if (!admitted || !wait_for_recovery_open(&opening) ||
        !opening.hasActiveTimeline() || opening.sequenceMarkers().size() != 40 ||
        discovery_admission_ms > 100.0 || open_admission_ms > 100.0) {
      return fail("recovery discovery and open admission should stay responsive while the matched large fixture verifies");
    }
    std::cout << "timeline recovery foreground latency: clips=128 checkpoints=40 journal_bytes="
              << QFileInfo(large_recovery.editJournalPath()).size()
              << " discovery_admission_ms=" << discovery_admission_ms
              << " open_admission_ms=" << open_admission_ms << '\n';
  }

  TimelineDocumentController author;
  author.createTimeline(QStringLiteral("Crash-safe Cut"));
  author.addClipFromGallery(
      QStringLiteral("photo:journal"),
      still_path,
      QStringLiteral("Opening"),
      4.0,
      QString{});
  if (!wait_for_source_integrity(&author)) {
    return fail("journal recovery should settle source integrity before saving its canonical snapshot");
  }
  if (!author.saveTimeline()) {
    return fail("recovery proof should establish a last known-good project");
  }
  const auto saved_path = author.savedDocumentPath();
  const auto journal_path = author.editJournalPath();
  const auto saved_before_recovery = read_file(saved_path);
  if (saved_before_recovery.isEmpty()) {
    return fail("the canonical project should be readable before recovery");
  }

  author.addSequenceMarker(QStringLiteral("Unsaved review note"));
  if (!author.flushEditJournal()) {
    return fail("a committed edit should flush to the journal");
  }

  QFile journal_file(journal_path);
  if (!journal_file.open(QIODevice::ReadOnly)) {
    return fail("the journal should be stored separately from the project");
  }
  QString previous_hash;
  auto entry_count = 0;
  while (!journal_file.atEnd()) {
    const auto line = journal_file.readLine().trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const auto parsed = QJsonDocument::fromJson(line);
    if (!parsed.isObject()) {
      return fail("every committed journal record should be standalone JSON");
    }
    auto entry = parsed.object();
    auto document = entry.value(QStringLiteral("document")).toObject();
    document.remove(QStringLiteral("updatedAt"));
    const auto stored_entry_hash = entry.take(QStringLiteral("entryHash")).toString();
    if (entry.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("timeline-edit-journal:v1") ||
        entry.value(QStringLiteral("previousEntryHash")).toString() != previous_hash ||
        entry.value(QStringLiteral("documentHash")).toString() != hash_object(document) ||
        stored_entry_hash != hash_object(entry)) {
      return fail("journal snapshots should form a deterministic SHA-256 chain");
    }
    previous_hash = stored_entry_hash;
    ++entry_count;
  }
  journal_file.close();
  if (entry_count < 2) {
    return fail("later edits should append without replacing the saved-state record");
  }

  if (!journal_file.open(QIODevice::WriteOnly | QIODevice::Append) ||
      journal_file.write("{\"truncated\":") <= 0) {
    return fail("recovery proof should inject a partial crash write");
  }
  journal_file.close();

  TimelineDocumentController recovery;
  QVariantMap candidate;
  for (const auto& value : recovery.recoveryCandidates()) {
    const auto current = value.toMap();
    if (current.value(QStringLiteral("sourcePath")).toString() == journal_path) {
      candidate = current;
      break;
    }
  }
  if (candidate.isEmpty() || !candidate.value(QStringLiteral("damagedTail")).toBool() ||
      candidate.value(QStringLiteral("validEntryCount")).toInt() != entry_count ||
      !candidate.value(QStringLiteral("openExplanation")).toString().contains(
          QStringLiteral("not changed until you save")) ||
      !candidate.value(QStringLiteral("restoreCopyExplanation")).toString().contains(
          QStringLiteral("original project remains untouched"))) {
    return fail("startup discovery should explain the latest valid snapshot before a damaged tail");
  }
  const auto initial_candidate_id =
      candidate.value(QStringLiteral("candidateId")).toString();
  if (!recovery.openRecoveryCandidate(initial_candidate_id) ||
      !wait_for_recovery_open(&recovery) || !recovery.dirty() ||
      recovery.sequenceMarkers().size() != 1 ||
      read_file(saved_path) != saved_before_recovery) {
    return fail("Open should recover the verified edit without changing the saved project");
  }
  const auto opened_recovery_document =
      photo_editor::desktop::TimelineHistoryTestAccess::document(recovery);
  if (!wait_for_journal_snapshot(&recovery, journal_path, opened_recovery_document)) {
    return fail("the recovered working state should receive a successful durable journal acknowledgement");
  }

  const auto recent_rows = [](TimelineDocumentController& controller) {
    controller.refreshRecentTimelines();
    QElapsedTimer timeout;
    timeout.start();
    while (controller.recentTimelinesLoading() && timeout.elapsed() < 5000)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return controller.recentTimelineRecords();
  };
  const auto timeline_id = candidate.value(QStringLiteral("timelineId")).toString();
  const auto initial_recent_rows = recent_rows(recovery);
  QVariantMap surfaced_recovery;
  for (const auto& value : initial_recent_rows) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("timelineId")).toString() == timeline_id &&
        row.value(QStringLiteral("isRecovery")).toBool()) {
      surfaced_recovery = row;
      break;
    }
  }
  const auto surfaced_candidate_id =
      surfaced_recovery.value(QStringLiteral("recoveryCandidateId")).toString();
  QVariantMap surfaced_candidate_metadata;
  for (const auto& value : recovery.recoveryCandidates()) {
    const auto metadata = value.toMap();
    if (metadata.value(QStringLiteral("candidateId")).toString() == surfaced_candidate_id) {
      surfaced_candidate_metadata = metadata;
      break;
    }
  }
  if (surfaced_candidate_id.isEmpty() || surfaced_candidate_metadata.isEmpty() ||
      surfaced_candidate_metadata.value(QStringLiteral("sourcePath")).toString() != journal_path ||
      surfaced_candidate_metadata.value(QStringLiteral("markerCount")).toInt() != 1) {
    return fail("Recent should surface the durable current recovery candidate for this timeline");
  }
  auto recovered_in_recents = false;
  // Force both timestamps into one second, with saved metadata lacking the
  // recovery's fractional precision. Text sorting incorrectly puts Z first.
  auto indexed_timelines = photo_editor::desktop::loadTimelineAssetRecords();
  for (auto& record : indexed_timelines) {
    if (record.document_path == saved_path)
      record.updated_at = QDateTime::fromString(
          surfaced_candidate_metadata.value(QStringLiteral("timestamp")).toString(),
          Qt::ISODateWithMs)
                              .toString(Qt::ISODate);
  }
  if (!photo_editor::desktop::saveTimelineAssetRecords(indexed_timelines))
    return fail("The same-second Recents fixture should update its isolated index");
  auto first_version_checked = false;
  QString latest_recent_candidate_id;
  for (const auto& value : recent_rows(recovery)) {
    const auto row = value.toMap();
    if (!first_version_checked && row.value(QStringLiteral("timelineId")) == timeline_id) {
      first_version_checked = true;
      if (!row.value(QStringLiteral("isRecovery")).toBool())
        return fail("Recents should sort newer recovery ahead of a saved version without milliseconds");
    }
    if (row.value(QStringLiteral("timelineId")).toString() == timeline_id &&
        row.value(QStringLiteral("isRecovery")).toBool()) {
      recovered_in_recents = row.value(QStringLiteral("isRecovery")).toBool();
      latest_recent_candidate_id =
          row.value(QStringLiteral("recoveryCandidateId")).toString();
    }
  }
  if (!recovered_in_recents || latest_recent_candidate_id != surfaced_candidate_id ||
      read_file(saved_path) != saved_before_recovery)
    return fail("Recent Timelines should expose recovery without rewriting the saved project");
  // A fresh controller simulates a later app session. Recents cannot depend on
  // the recovery dialog having been opened in the previous session.
  TimelineDocumentController later_session;
  recovered_in_recents = false;
  QString later_session_candidate_id;
  for (const auto& value : recent_rows(later_session)) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("timelineId")).toString() == timeline_id &&
        row.value(QStringLiteral("isRecovery")).toBool()) {
      recovered_in_recents = true;
      later_session_candidate_id =
          row.value(QStringLiteral("recoveryCandidateId")).toString();
      break;
    }
  }
  if (!recovered_in_recents || later_session_candidate_id.isEmpty() ||
      !later_session.openRecoveryCandidate(later_session_candidate_id) ||
      !wait_for_recovery_open(&later_session) ||
      later_session.sequenceMarkers().size() != 1 || read_file(saved_path) != saved_before_recovery)
    return fail("A later session should reopen the recovered state from Recents with saved bytes intact");
  const auto later_session_document =
      photo_editor::desktop::TimelineHistoryTestAccess::document(later_session);
  if (!wait_for_journal_snapshot(&later_session, journal_path, later_session_document)) {
    return fail("the state reopened from Recent should remain durably recoverable in the next session");
  }
  QString current_recovery_candidate_id;
  for (const auto& value : recent_rows(later_session)) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("timelineId")).toString() == timeline_id &&
        row.value(QStringLiteral("isRecovery")).toBool()) {
      current_recovery_candidate_id =
          row.value(QStringLiteral("recoveryCandidateId")).toString();
      break;
    }
  }
  if (current_recovery_candidate_id.isEmpty())
    return fail("the reopened working state should remain available as the current Recent recovery");
  QFile damaged_project(saved_path);
  if (!damaged_project.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      damaged_project.write("{damaged-project") <= 0) {
    return fail("recovery proof should inject canonical project corruption");
  }
  damaged_project.close();

  TimelineDocumentController copied;
  if (!copied.restoreRecoveryCandidateAsCopy(current_recovery_candidate_id) ||
      copied.savedDocumentPath() == saved_path ||
      !QFileInfo::exists(copied.savedDocumentPath()) ||
      !copied.timelineName().contains(QStringLiteral("Recovered Copy")) ||
      copied.sequenceMarkers().size() != 1) {
    return fail("Restore Copy should survive project corruption with a new durable identity");
  }
  if (!copied.discardRecoveryCandidate(current_recovery_candidate_id)) {
    return fail("Discard should archive the journal recovery");
  }
  for (const auto& value : copied.recoveryCandidates()) {
    if (value.toMap().value(QStringLiteral("candidateId")).toString() == current_recovery_candidate_id) {
      return fail("discarded recovery should no longer be offered");
    }
  }

  auto copy_in_recents = false;
  for (const auto& value : recent_rows(copied)) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("recoveryCandidateId")).toString() == current_recovery_candidate_id)
      return fail("Discarded recovery should disappear from Recent Timelines");
    copy_in_recents |= row.value(QStringLiteral("documentPath")).toString() == copied.savedDocumentPath();
  }
  if (!copy_in_recents)
    return fail("Restore Copy should immediately register a saved Recent Timeline");
  // The registered no-argument test continues through media, editing, and
  // delivery coverage. This mode ends after the complete Save/recovery,
  // append-only checkpoint, Recent reopen, Restore Copy, and Discard contract.
  if (recovery_lifecycle_only) {
    std::cout << "timeline recovery lifecycle segment: passed\n";
    return EXIT_SUCCESS;
  }
  TimelineDocumentController recursive_compound_author;
  recursive_compound_author.createTimeline(QStringLiteral("Recursive Compound Guard"));
  recursive_compound_author.addClipFromGallery(
      QStringLiteral("photo:recursive"), still_path,
      QStringLiteral("Recursive Candidate"), 2.0, QString{});
  if (!recursive_compound_author.saveTimeline()) {
    return fail("recursive-compound proof should save a source timeline");
  }
  const auto recursive_path = recursive_compound_author.savedDocumentPath();
  QFile recursive_file(recursive_path);
  if (!recursive_file.open(QIODevice::ReadOnly)) {
    return fail("recursive-compound proof should read the saved timeline");
  }
  auto recursive_document = QJsonDocument::fromJson(recursive_file.readAll());
  recursive_file.close();
  auto recursive_root = recursive_document.object();
  auto recursive_tracks = recursive_root.value(QStringLiteral("tracks")).toArray();
  auto recursive_video_track = recursive_tracks.at(0).toObject();
  auto recursive_clips = recursive_video_track.value(QStringLiteral("clips")).toArray();
  auto recursive_clip = recursive_clips.at(0).toObject();
  recursive_clip.insert(QStringLiteral("compoundDocumentPath"), recursive_path);
  recursive_clip.insert(
      QStringLiteral("compoundRenderPath"),
      workspace.filePath(QStringLiteral("recursive-preview.mp4")));
  recursive_clips[0] = recursive_clip;
  recursive_video_track.insert(QStringLiteral("clips"), recursive_clips);
  recursive_tracks[0] = recursive_video_track;
  recursive_root.insert(QStringLiteral("tracks"), recursive_tracks);
  if (!recursive_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      recursive_file.write(QJsonDocument(recursive_root).toJson(QJsonDocument::Indented)) <= 0) {
    return fail("recursive-compound proof should inject a self-referencing child path");
  }
  recursive_file.close();
  TimelineDocumentController recursive_compound;
  if (!recursive_compound.loadTimeline(recursive_path) ||
      !recursive_compound.currentClip().value(QStringLiteral("isCompound")).toBool() ||
      recursive_compound.openSelectedCompoundClip() || recursive_compound.insideCompound() ||
      recursive_compound.compoundNavigationDepth() != 0 ||
      !recursive_compound.status().contains(QStringLiteral("recursive reference"),
                                            Qt::CaseInsensitive)) {
    return fail("compound navigation should reject a document that recursively points to itself without changing context");
  }

  const auto health_source = workspace.filePath(QStringLiteral("health-source.jpg"));
  QFile health_source_file(health_source);
  if (!health_source_file.open(QIODevice::WriteOnly) ||
      health_source_file.write("health-fixture") <= 0) {
    return fail("project-health proof should create source media");
  }
  health_source_file.close();
  TimelineDocumentController health;
  health.createTimeline(QStringLiteral("Health Scan"));
  health.addClipFromGallery(
      QStringLiteral("photo:health"),
      health_source,
      QStringLiteral("Offline Product Shot"),
      4.0,
      QString{});
  health.selectClip(0);
  health.addSelectedClipEffect(QStringLiteral("Unavailable Plugin"));
  health.addTitleClip();
  health.updateSelectedTitle(QVariantMap{
      {QStringLiteral("fontFamily"), QStringLiteral("Definitely Missing Font 9Z")},
  });
  if (!QFile::remove(health_source)) {
    return fail("project-health proof should take media offline");
  }
  const auto health_result = health.timelineProjectHealth();
  QSet<QString> health_issue_keys;
  for (const auto& value : health_result.value(QStringLiteral("issues")).toList()) {
    const auto issue = value.toMap();
    health_issue_keys.insert(issue.value(QStringLiteral("key")).toString());
    if (issue.value(QStringLiteral("message")).toString().isEmpty() ||
        issue.value(QStringLiteral("category")).toString().isEmpty() ||
        issue.value(QStringLiteral("repair")).toString().isEmpty()) {
      return fail("each project-health issue should explain scope and a deterministic next action");
    }
  }
  if (health_result.value(QStringLiteral("ok")).toBool() ||
      health_result.value(QStringLiteral("blockingCount")).toInt() < 3 ||
      !health_issue_keys.contains(QStringLiteral("missing-media")) ||
      !health_issue_keys.contains(QStringLiteral("unsupported-effect")) ||
      !health_issue_keys.contains(QStringLiteral("missing-font")) ||
      health_result.value(QStringLiteral("freeDiskBytes")).toLongLong() <= 0) {
    return fail("project health should block on offline media, unavailable effects, and missing fonts while reporting disk capacity");
  }
  const auto health_preflight = health.timelineDeliveryPreflight();
  QSet<QString> health_preflight_keys;
  for (const auto& value : health_preflight.value(QStringLiteral("issues")).toList()) {
    health_preflight_keys.insert(value.toMap().value(QStringLiteral("key")).toString());
  }
  if (health_preflight.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-delivery-preflight:v3") ||
      health_preflight.value(QStringLiteral("ok")).toBool() ||
      !health_preflight_keys.contains(QStringLiteral("missing-media")) ||
      !health_preflight_keys.contains(QStringLiteral("unsupported-effect")) ||
      !health_preflight_keys.contains(QStringLiteral("missing-font")) ||
      health_preflight.value(QStringLiteral("offlineMediaCount")).toInt() != 1 ||
      health_preflight.value(QStringLiteral("unavailableEffectCount")).toInt() != 1 ||
      health_preflight.value(QStringLiteral("missingFontCount")).toInt() != 1 ||
      health_preflight.value(QStringLiteral("freeDiskBytes")).toLongLong() <= 0 ||
      health_preflight.value(QStringLiteral("estimatedOutputBytes")).toLongLong() <= 0) {
    return fail("delivery preflight should block offline media, fonts, and effects while sizing the real destination");
  }
  const auto blocked_publish = health.timelinePublishSummary();
  if (blocked_publish.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-publish-summary:v1") ||
      blocked_publish.value(QStringLiteral("publishEnabled")).toBool() ||
      blocked_publish.value(QStringLiteral("format")).toString().isEmpty() ||
      blocked_publish.value(QStringLiteral("width")).toInt() != 1080 ||
      blocked_publish.value(QStringLiteral("height")).toInt() != 1350 ||
      blocked_publish.value(QStringLiteral("durationSeconds")).toDouble() <= 0.0 ||
      blocked_publish.value(QStringLiteral("rangeLabel")).toString() !=
          QStringLiteral("Entire sequence") ||
      blocked_publish.value(QStringLiteral("destinationPath")).toString().isEmpty()) {
    return fail("publish should always disclose format, size, duration, range, and destination and refuse blockers");
  }

  const auto flash_source = workspace.filePath(QStringLiteral("flash-screen.png"));
  QImage flash_frame(24, 24, QImage::Format_RGB32);
  flash_frame.fill(QColor(240, 240, 240));
  if (!flash_frame.save(flash_source)) {
    return fail("delivery preflight proof should create a rapid-cut source frame");
  }
  TimelineDocumentController flash_screen;
  flash_screen.createTimeline(QStringLiteral("Rapid Cut Screen"));
  for (auto index = 0; index < 4; ++index) {
    flash_screen.addClipFromGallery(
        QStringLiteral("photo:flash-%1").arg(index), flash_source,
        QStringLiteral("Rapid frame %1").arg(index + 1), 0.20, QString{});
  }
  const auto flash_preflight = flash_screen.timelineDeliveryPreflight();
  auto flash_disclosure_found = false;
  for (const auto& value : flash_preflight.value(QStringLiteral("issues")).toList()) {
    const auto issue = value.toMap();
    if (issue.value(QStringLiteral("key")).toString() == QStringLiteral("flash-risk") &&
        issue.value(QStringLiteral("message")).toString().contains(
            QStringLiteral("cannot replace a frame-luminance"))) {
      flash_disclosure_found = true;
      break;
    }
  }
  if (flash_preflight.value(QStringLiteral("rapidCutWindowCount")).toInt() <= 0 ||
      !flash_disclosure_found) {
    return fail("delivery preflight should flag rapid picture changes without overstating edit-graph analysis");
  }

  const auto relink_original_root = workspace.filePath(QStringLiteral("relink-original"));
  const auto relink_search_root = workspace.filePath(QStringLiteral("relink-search"));
  const auto relink_good_root = QDir(relink_search_root).filePath(QStringLiteral("camera-a"));
  const auto relink_decoy_root = QDir(relink_search_root).filePath(QStringLiteral("camera-b"));
  QDir().mkpath(relink_original_root);
  QDir().mkpath(relink_good_root);
  QDir().mkpath(relink_decoy_root);
  const auto relink_original =
      QDir(relink_original_root).filePath(QStringLiteral("A001.png"));
  QImage relink_source(24, 16, QImage::Format_RGB32);
  relink_source.fill(QColor(18, 164, 92));
  if (!relink_source.save(relink_original)) {
    return fail("batch relink proof should create its source media");
  }
  TimelineDocumentController relink;
  relink.createTimeline(QStringLiteral("Batch Relink"));
  relink.addClipFromGallery(
      QStringLiteral("photo:relink"), relink_original,
      QStringLiteral("Camera A001"), 4.0, QString{});
  if (!relink.saveTimeline()) {
    return fail("batch relink proof should persist source identity before media goes offline");
  }
  const auto relink_saved_path = relink.savedDocumentPath();
  const auto relink_good = QDir(relink_good_root).filePath(QStringLiteral("A001.png"));
  if (!QFile::rename(relink_original, relink_good)) {
    return fail("batch relink proof should move the genuine source into a candidate root");
  }
  QImage relink_decoy(24, 16, QImage::Format_RGB32);
  relink_decoy.fill(QColor(192, 28, 42));
  const auto relink_bad = QDir(relink_decoy_root).filePath(QStringLiteral("A001.png"));
  if (!relink_decoy.save(relink_bad) ||
      relink.currentClip().value(QStringLiteral("mediaOnline")).toBool()) {
    return fail("batch relink proof should include one offline source and one same-name decoy");
  }
  const auto relink_preview =
      relink.previewBatchRelink(QStringList{relink_search_root});
  const auto substitutions =
      relink_preview.value(QStringLiteral("substitutions")).toList();
  if (relink_preview.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-batch-relink:v1") ||
      substitutions.size() != 1 ||
      !substitutions.first().toMap().value(QStringLiteral("recommended")).toBool() ||
      substitutions.first().toMap().value(QStringLiteral("candidatePath")).toString() !=
          relink_good ||
      relink.currentClip().value(QStringLiteral("filePath")).toString() !=
          relink_original) {
    return fail("batch relink preview should recommend only the identity-matched candidate without changing the edit");
  }
  auto rejected_substitution = substitutions.first().toMap();
  rejected_substitution.insert(QStringLiteral("approved"), true);
  rejected_substitution.insert(QStringLiteral("candidatePath"), relink_bad);
  if (relink.applyBatchRelink(QVariantList{rejected_substitution}) != 0 ||
      relink.currentClip().value(QStringLiteral("mediaOnline")).toBool()) {
    return fail("an approved same-name decoy should still be rejected by saved media identity");
  }
  auto approved_substitution = substitutions.first().toMap();
  approved_substitution.insert(QStringLiteral("approved"), true);
  if (relink.applyBatchRelink(QVariantList{approved_substitution}) != 1 ||
      !relink.currentClip().value(QStringLiteral("mediaOnline")).toBool() ||
      relink.currentClip().value(QStringLiteral("filePath")).toString() != relink_good) {
    return fail("an explicitly approved identity match should relink every use of the offline source");
  }
  relink.undo();
  if (relink.currentClip().value(QStringLiteral("filePath")).toString() != relink_original) {
    return fail("batch relink should undo atomically back to the original offline path");
  }
  relink.redo();
  if (relink.currentClip().value(QStringLiteral("filePath")).toString() != relink_good ||
      !relink.saveTimeline()) {
    return fail("batch relink should redo and save the verified replacement");
  }
  TimelineDocumentController relink_reopened;
  if (!relink_reopened.loadTimeline(relink_saved_path) ||
      relink_reopened.currentClip().value(QStringLiteral("filePath")).toString() !=
          relink_good ||
      !relink_reopened.currentClip().value(QStringLiteral("mediaOnline")).toBool()) {
    return fail("batch relink substitutions and source identity should survive save and reopen");
  }

  TimelineDocumentController bins;
  bins.createTimeline(QStringLiteral("Organized Media"));
  bins.addClipFromGallery(
      QStringLiteral("asset:interview"), still_path,
      QStringLiteral("Interview"), 3.0, QString{});
  bins.addClipFromGallery(
      QStringLiteral("asset:b-roll"), still_path,
      QStringLiteral("B-roll"), 3.0, QString{});
  const auto footage_bin = bins.createMediaBin(QStringLiteral("Footage"));
  const auto duplicate_bin = bins.createMediaBin(QStringLiteral("Footage"));
  const auto selects_bin = bins.createMediaBin(QStringLiteral("Selects"), footage_bin);
  if (footage_bin.isEmpty() || duplicate_bin.isEmpty() || selects_bin.isEmpty() ||
      bins.mediaBins().size() != 3 ||
      bins.mediaBins().at(1).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Footage 2") ||
      bins.mediaBins().at(2).toMap().value(QStringLiteral("depth")).toInt() != 1) {
    return fail("media bins should support nested folders and deterministic sibling names");
  }
  if (!bins.assignMediaAssetToBin(QStringLiteral("asset:interview"), selects_bin) ||
      !bins.assignMediaAssetToBin(QStringLiteral("asset:b-roll"), duplicate_bin) ||
      !bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), footage_bin) ||
      bins.mediaAssetVisibleInBin(QStringLiteral("asset:b-roll"), footage_bin) ||
      bins.moveMediaBin(footage_bin, selects_bin)) {
    return fail("bin filtering should include descendants while cycle guards reject invalid nesting");
  }
  if (!bins.moveMediaBin(selects_bin, duplicate_bin) ||
      bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), footage_bin) ||
      !bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), duplicate_bin)) {
    return fail("drag-equivalent bin moves should update descendant filtering immediately");
  }
  bins.undo();
  if (!bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), footage_bin)) {
    return fail("nested bin moves should undo atomically");
  }
  bins.redo();
  if (!bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), duplicate_bin)) {
    return fail("nested bin moves should redo atomically");
  }
  if (!bins.deleteMediaBin(selects_bin) ||
      !bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), duplicate_bin)) {
    return fail("deleting a bin should retain its contents in the parent");
  }
  bins.undo();
  if (bins.mediaBins().size() != 3 ||
      !bins.mediaAssetVisibleInBin(QStringLiteral("asset:interview"), selects_bin)) {
    return fail("bin deletion and asset reassignment should restore together on undo");
  }
  const QStringList organized_assets{
      QStringLiteral("asset:interview"), QStringLiteral("asset:b-roll")};
  if (!bins.setMediaAssetRating(organized_assets, 4) ||
      !bins.setMediaAssetColorLabel(organized_assets, QStringLiteral("green")) ||
      !bins.setMediaAssetDisposition(
          {QStringLiteral("asset:interview")}, QStringLiteral("favorite")) ||
      !bins.setMediaAssetDisposition(
          {QStringLiteral("asset:b-roll")}, QStringLiteral("rejected")) ||
      !bins.setMediaAssetTags(
          {QStringLiteral("asset:interview")},
          {QStringLiteral("Interview"), QStringLiteral("Client approved"),
           QStringLiteral("interview")})) {
    return fail("ratings, marks, color labels, and normalized user tags should support batch commands");
  }
  const auto interview_metadata =
      bins.mediaAssetMetadata(QStringLiteral("asset:interview"));
  const auto broll_metadata = bins.mediaAssetMetadata(QStringLiteral("asset:b-roll"));
  if (interview_metadata.value(QStringLiteral("rating")).toInt() != 4 ||
      !interview_metadata.value(QStringLiteral("favorite")).toBool() ||
      interview_metadata.value(QStringLiteral("rejected")).toBool() ||
      interview_metadata.value(QStringLiteral("colorLabel")).toString() !=
          QStringLiteral("green") ||
      interview_metadata.value(QStringLiteral("tags")).toStringList() !=
          QStringList{QStringLiteral("Interview"), QStringLiteral("Client approved")} ||
      broll_metadata.value(QStringLiteral("rating")).toInt() != 4 ||
      !broll_metadata.value(QStringLiteral("rejected")).toBool()) {
    return fail("media metadata should publish deterministic batch state with case-insensitive tag deduplication");
  }
  bins.undo();
  if (!bins.mediaAssetMetadata(QStringLiteral("asset:interview"))
           .value(QStringLiteral("tags")).toStringList().isEmpty()) {
    return fail("a batch tag edit should undo in one step");
  }
  bins.redo();
  if (bins.mediaAssetMetadata(QStringLiteral("asset:interview"))
          .value(QStringLiteral("tags")).toStringList().size() != 2) {
    return fail("a batch tag edit should redo without changing ratings or marks");
  }
  bins.seekSequenceSeconds(1.0);
  const auto search_marker_id = bins.addSequenceMarker(QStringLiteral("Best quote"));
  bins.setSequenceMarkerNote(search_marker_id, QStringLiteral("Use in campaign opener"));
  bins.generateCaptionsFromTranscript(
      QStringLiteral("Launch morning briefing. Ocean sunset closing."));
  if (!bins.setMediaAssetDetectedOnScreenText(
          QStringLiteral("asset:interview"),
          {QStringLiteral("Mountain Lodge"), QStringLiteral("SUMMER 2026"),
           QStringLiteral("mountain lodge")})) {
    return fail("media search proof should accept normalized detected on-screen text");
  }
  const QVariantMap interview_catalog_metadata{
      {QStringLiteral("cameraLabel"), QStringLiteral("A-Camera Interview")},
      {QStringLiteral("filePath"), QStringLiteral("/shoot/day-01/interview-a.mov")},
      {QStringLiteral("captureDate"), QStringLiteral("2026-07-19")},
      {QStringLiteral("lensModel"), QStringLiteral("Summilux 35mm")},
      {QStringLiteral("locationPath"), QStringLiteral("Denver / Studio A")},
      {QStringLiteral("projectSummary"), QStringLiteral("Summer launch campaign")},
      {QStringLiteral("projectNames"), QStringList{QStringLiteral("Launch Film")}},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("durationText"), QStringLiteral("0:03")},
  };
  const auto search_matches = [&bins, &interview_catalog_metadata](
                                  const QString& query,
                                  const QStringList& scopes,
                                  const QString& asset_id = QStringLiteral("asset:interview")) {
    return bins.searchMediaAsset(asset_id, query, scopes, interview_catalog_metadata)
        .value(QStringLiteral("matches")).toBool();
  };
  if (!search_matches(QStringLiteral("interview-a.mov"), {QStringLiteral("filename")}) ||
      !search_matches(QStringLiteral("summilux studio"), {QStringLiteral("metadata")}) ||
      !search_matches(QStringLiteral("client approved"), {QStringLiteral("tags")}) ||
      !search_matches(QStringLiteral("briefing"), {QStringLiteral("transcript")}) ||
      !search_matches(QStringLiteral("launch morning"), {QStringLiteral("captions")}) ||
      !search_matches(QStringLiteral("campaign opener"), {QStringLiteral("markers")}) ||
      !search_matches(QStringLiteral("mountain lodge"), {QStringLiteral("text")}) ||
      !search_matches(QStringLiteral("approved \"mountain lodge\""),
                      {QStringLiteral("all")}) ||
      search_matches(QStringLiteral("mountain lodge"), {QStringLiteral("tags")}) ||
      search_matches(QStringLiteral("campaign opener"), {QStringLiteral("markers")},
                     QStringLiteral("asset:b-roll"))) {
    return fail("one media field should search every indexed source, compose terms, honor quoted phrases and filter chips, and keep marker hits attached to the correct underlying clip");
  }
  const auto on_screen_metadata =
      bins.mediaAssetMetadata(QStringLiteral("asset:interview"))
          .value(QStringLiteral("detectedOnScreenText")).toStringList();
  if (on_screen_metadata !=
      QStringList{QStringLiteral("Mountain Lodge"), QStringLiteral("SUMMER 2026")}) {
    return fail("detected text should deduplicate case-insensitively before indexing");
  }
  if (!bins.setMediaAssetNotesAndCustomFields(
          QStringLiteral("asset:interview"),
          QStringLiteral("  Director prefers the second answer.  "),
          QVariantMap{
              {QStringLiteral("Campaign"), QStringLiteral("Summer Launch")},
              {QStringLiteral("Client status"), QStringLiteral("Approved")},
              {QStringLiteral("campaign"), QStringLiteral("duplicate ignored")},
              {QStringLiteral("Empty"), QString{}},
          })) {
    return fail("contextual metadata should accept bounded notes and custom fields");
  }
  const auto editorial_metadata =
      bins.mediaAssetMetadata(QStringLiteral("asset:interview"));
  if (editorial_metadata.value(QStringLiteral("notes")).toString() !=
          QStringLiteral("Director prefers the second answer.") ||
      editorial_metadata.value(QStringLiteral("customFields")).toMap().size() != 2 ||
      !search_matches(QStringLiteral("second answer summer launch"),
                      {QStringLiteral("metadata")})) {
    return fail("notes and custom fields should normalize deterministically and join unified metadata search");
  }
  bins.undo();
  if (!bins.mediaAssetMetadata(QStringLiteral("asset:interview"))
           .value(QStringLiteral("notes")).toString().isEmpty()) {
    return fail("notes and custom fields should undo together in one project edit");
  }
  bins.redo();
  auto smart_interview_asset = interview_catalog_metadata;
  smart_interview_asset.insert(
      QStringLiteral("assetId"), QStringLiteral("asset:interview"));
  smart_interview_asset.insert(QStringLiteral("durationSeconds"), 3.0);
  smart_interview_asset.insert(QStringLiteral("sourceWidth"), 1920);
  smart_interview_asset.insert(QStringLiteral("sourceHeight"), 1080);
  smart_interview_asset.insert(QStringLiteral("online"), true);
  const QVariantMap approved_launch_rules{
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("minimumRating"), 4},
      {QStringLiteral("colorLabel"), QStringLiteral("green")},
      {QStringLiteral("tag"), QStringLiteral("client approved")},
      {QStringLiteral("dateFrom"), QStringLiteral("2026-07-01")},
      {QStringLiteral("dateTo"), QStringLiteral("2026-07-31")},
      {QStringLiteral("minimumDuration"), 2.0},
      {QStringLiteral("maximumDuration"), 4.0},
      {QStringLiteral("orientation"), QStringLiteral("landscape")},
      {QStringLiteral("onlineState"), QStringLiteral("online")},
      {QStringLiteral("usage"), QStringLiteral("used")},
      {QStringLiteral("customField"), QStringLiteral("campaign")},
      {QStringLiteral("customValue"), QStringLiteral("summer")},
  };
  const auto approved_collection = bins.createSmartCollection(
      QStringLiteral("Client Selects"), approved_launch_rules);
  if (approved_collection.isEmpty() || bins.smartCollections().size() != 1 ||
      bins.activeSmartCollectionId() != approved_collection ||
      !bins.mediaAssetMatchesSmartCollection(
          smart_interview_asset, approved_collection)) {
    return fail("smart collections should combine media type, rating, label, tag, date, duration, orientation, online, usage, and custom-field rules");
  }
  auto nonmatching_asset = smart_interview_asset;
  nonmatching_asset.insert(QStringLiteral("assetId"), QStringLiteral("asset:b-roll"));
  if (bins.mediaAssetMatchesSmartCollection(nonmatching_asset, approved_collection)) {
    return fail("smart collection evaluation should use the candidate asset's editorial metadata rather than leaking another asset's tags or custom fields");
  }
  const auto duplicate_collection = bins.createSmartCollection(
      QStringLiteral("Client Selects"),
      QVariantMap{
          {QStringLiteral("onlineState"), QStringLiteral("offline")},
          {QStringLiteral("usage"), QStringLiteral("unused")},
      });
  const QVariantMap offline_unused_asset{
      {QStringLiteral("assetId"), QStringLiteral("asset:offline-unused")},
      {QStringLiteral("filePath"), QStringLiteral("/missing/offline.mov")},
      {QStringLiteral("mediaType"), QStringLiteral("video")},
      {QStringLiteral("online"), false},
      {QStringLiteral("durationSeconds"), 12.0},
  };
  if (duplicate_collection.isEmpty() || bins.smartCollections().size() != 2 ||
      bins.smartCollections().at(1).toMap().value(QStringLiteral("name")).toString() !=
          QStringLiteral("Client Selects 2") ||
      !bins.mediaAssetMatchesSmartCollection(
          offline_unused_asset, duplicate_collection)) {
    return fail("smart collection names should deduplicate while offline and unused rules remain live");
  }
  if (!bins.deleteSmartCollection(duplicate_collection) ||
      bins.smartCollections().size() != 1) {
    return fail("smart collections should delete explicitly");
  }
  bins.undo();
  if (bins.smartCollections().size() != 2) {
    return fail("smart collection deletion should undo atomically");
  }
  bins.redo();
  if (bins.smartCollections().size() != 1 ||
      !bins.updateSmartCollection(
          approved_collection, QStringLiteral("Approved Launch"),
          approved_launch_rules)) {
    return fail("smart collection deletion should redo and saved rules should remain editable");
  }
  bins.undo();
  if (bins.smartCollections().at(0).toMap().value(QStringLiteral("name")).toString() !=
      QStringLiteral("Client Selects")) {
    return fail("smart collection rule and name updates should undo together");
  }
  bins.redo();
  bins.setActiveSmartCollectionId(approved_collection);
  const auto opening_review_range = bins.createReviewRange(
      QStringLiteral("Opening Review"), 0.5, 2.5, 0.25, 0.5);
  if (opening_review_range.isEmpty() || bins.reviewRanges().size() != 1 ||
      std::abs(bins.reviewRanges().first().toMap()
                   .value(QStringLiteral("prerollSeconds")).toDouble() - 0.2666667) > 0.001) {
    return fail("saved review ranges should retain frame-snapped In, Out, preroll, and postroll values");
  }
  if (!bins.updateReviewRange(
          opening_review_range, QStringLiteral("Opening Approval"),
          0.4, 2.6, 0.5, 0.75)) {
    return fail("saved review ranges should remain editable through one controller command");
  }
  bins.undo();
  if (bins.reviewRanges().first().toMap().value(QStringLiteral("name")).toString() !=
      QStringLiteral("Opening Review")) {
    return fail("saved review range updates should undo atomically");
  }
  bins.redo();
  bins.setActiveMediaBinId(duplicate_bin);
  if (!bins.saveTimeline()) {
    return fail("organized media proof should save the bin hierarchy");
  }
  TimelineDocumentController bins_reopened;
  if (!bins_reopened.loadTimeline(bins.savedDocumentPath()) ||
      bins_reopened.mediaBins().size() != 3 ||
      bins_reopened.smartCollections().size() != 1 ||
      bins_reopened.reviewRanges().size() != 1 ||
      bins_reopened.activeMediaBinId() != duplicate_bin ||
      bins_reopened.activeSmartCollectionId() != approved_collection ||
      bins_reopened.smartCollections().at(0).toMap()
              .value(QStringLiteral("name")).toString() !=
          QStringLiteral("Approved Launch") ||
      bins_reopened.reviewRanges().first().toMap()
              .value(QStringLiteral("name")).toString() !=
          QStringLiteral("Opening Approval") ||
      !bins_reopened.mediaAssetMatchesSmartCollection(
          smart_interview_asset, approved_collection) ||
      !bins_reopened.mediaAssetVisibleInBin(
          QStringLiteral("asset:interview"), duplicate_bin) ||
      bins_reopened.mediaAssetVisibleInBin(
          QStringLiteral("asset:b-roll"), footage_bin) ||
      bins_reopened.mediaAssetMetadata(QStringLiteral("asset:interview"))
              .value(QStringLiteral("rating")).toInt() != 4 ||
      !bins_reopened.mediaAssetMetadata(QStringLiteral("asset:interview"))
               .value(QStringLiteral("favorite")).toBool() ||
      bins_reopened.mediaAssetMetadata(QStringLiteral("asset:interview"))
              .value(QStringLiteral("tags")).toStringList().size() != 2 ||
      !bins_reopened.searchMediaAsset(
           QStringLiteral("asset:interview"), QStringLiteral("mountain lodge"),
           {QStringLiteral("text")}, interview_catalog_metadata)
           .value(QStringLiteral("matches")).toBool() ||
      !bins_reopened.searchMediaAsset(
           QStringLiteral("asset:interview"), QStringLiteral("campaign opener"),
           {QStringLiteral("markers")}, interview_catalog_metadata)
           .value(QStringLiteral("matches")).toBool() ||
      bins_reopened.mediaAssetMetadata(QStringLiteral("asset:interview"))
              .value(QStringLiteral("notes")).toString() !=
          QStringLiteral("Director prefers the second answer.") ||
      bins_reopened.mediaAssetMetadata(QStringLiteral("asset:interview"))
              .value(QStringLiteral("customFields")).toMap().size() != 2 ||
      !bins_reopened.mediaAssetMetadata(QStringLiteral("asset:b-roll"))
               .value(QStringLiteral("rejected")).toBool()) {
    return fail("nested bins plus ratings, labels, marks, and tags should survive save and reopen");
  }

  TimelineDocumentController media_usage;
  media_usage.createTimeline(QStringLiteral("Media Usage"));
  media_usage.addClipFromGallery(
      QStringLiteral("usage:duplicate-used"), still_path,
      QStringLiteral("Duplicate Used"), 3.0, QString{});
  const QVariantMap canonical_usage_asset{
      {QStringLiteral("assetId"), QStringLiteral("usage:canonical")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Canonical Take")},
      {QStringLiteral("filePath"), still_path},
      {QStringLiteral("durationSeconds"), 3.0},
      {QStringLiteral("online"), true},
      {QStringLiteral("duplicateGroupKey"), QStringLiteral("usage-group-a")},
  };
  const QVariantMap duplicate_used_asset{
      {QStringLiteral("assetId"), QStringLiteral("usage:duplicate-used")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Duplicate Used")},
      {QStringLiteral("filePath"), still_path},
      {QStringLiteral("durationSeconds"), 3.0},
      {QStringLiteral("online"), true},
      {QStringLiteral("duplicateGroupKey"), QStringLiteral("usage-group-a")},
  };
  const QVariantMap unused_usage_asset{
      {QStringLiteral("assetId"), QStringLiteral("usage:unused")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Unused B-roll")},
      {QStringLiteral("filePath"), still_path},
      {QStringLiteral("durationSeconds"), 3.0},
      {QStringLiteral("online"), true},
  };
  const QVariantMap offline_derivative_asset{
      {QStringLiteral("assetId"), QStringLiteral("usage:offline-derivative")},
      {QStringLiteral("cameraLabel"), QStringLiteral("Offline Select")},
      {QStringLiteral("filePath"), workspace.filePath(QStringLiteral("missing-select.mov"))},
      {QStringLiteral("durationSeconds"), 3.0},
      {QStringLiteral("online"), false},
      {QStringLiteral("virtualCopy"), true},
      {QStringLiteral("sourceAssetId"), QStringLiteral("usage:canonical")},
  };
  const QVariantList usage_catalog{
      canonical_usage_asset, duplicate_used_asset, unused_usage_asset,
      offline_derivative_asset};
  const auto usage_analysis = media_usage.analyzeMediaUsage(usage_catalog);
  const auto usage_summary = usage_analysis.value(QStringLiteral("summary")).toMap();
  if (usage_analysis.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-media-usage:v1") ||
      usage_summary.value(QStringLiteral("used")).toInt() != 1 ||
      usage_summary.value(QStringLiteral("unused")).toInt() != 3 ||
      usage_summary.value(QStringLiteral("duplicate")).toInt() != 2 ||
      usage_summary.value(QStringLiteral("offline")).toInt() != 1 ||
      usage_summary.value(QStringLiteral("derivative")).toInt() != 1 ||
      !media_usage.revealMediaUsage(QStringLiteral("usage:duplicate-used"))
           .value(QStringLiteral("timelineUsed")).toBool()) {
    return fail("media usage should classify and reveal used, unused, duplicate, offline, and derivative assets deterministically");
  }
  const auto can_undo_before_rejected_remove = media_usage.canUndo();
  if (media_usage.removeUnusedMediaFromProject(
          {QStringLiteral("usage:duplicate-used")}) != 0 ||
      media_usage.canUndo() != can_undo_before_rejected_remove) {
    return fail("media usage cleanup should reject used media without creating an undo entry");
  }
  if (media_usage.removeUnusedMediaFromProject(
          {QStringLiteral("usage:unused")}) != 1 ||
      media_usage.mediaAssetVisibleInBin(QStringLiteral("usage:unused"), QString{})) {
    return fail("unused media cleanup should remove only verified-unused project references");
  }
  media_usage.undo();
  if (!media_usage.mediaAssetVisibleInBin(QStringLiteral("usage:unused"), QString{})) {
    return fail("unused media cleanup should restore project visibility in one undo");
  }
  media_usage.redo();
  if (media_usage.mediaAssetVisibleInBin(QStringLiteral("usage:unused"), QString{})) {
    return fail("unused media cleanup should redo deterministically");
  }
  const auto consolidated_usage_count = media_usage.consolidateDuplicateMedia(
      canonical_usage_asset, {duplicate_used_asset});
  const auto consolidated_usage_asset_id =
      media_usage.currentClip().value(QStringLiteral("assetId")).toString();
  const auto consolidated_duplicate_visible = media_usage.mediaAssetVisibleInBin(
      QStringLiteral("usage:duplicate-used"), QString{});
  if (consolidated_usage_count != 1 ||
      consolidated_usage_asset_id !=
          QStringLiteral("usage:canonical") ||
      consolidated_duplicate_visible) {
    return fail("duplicate consolidation should replace sequence uses with an explicit online canonical asset and remove only duplicate project entries");
  }
  media_usage.undo();
  if (media_usage.currentClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("usage:duplicate-used") ||
      !media_usage.mediaAssetVisibleInBin(
          QStringLiteral("usage:duplicate-used"), QString{})) {
    return fail("duplicate consolidation should restore clip identity and project visibility together on undo");
  }
  media_usage.redo();
  if (!media_usage.saveTimeline()) {
    return fail("media usage cleanup should save the consolidated project state");
  }
  TimelineDocumentController media_usage_reopened;
  if (!media_usage_reopened.loadTimeline(media_usage.savedDocumentPath()) ||
      media_usage_reopened.currentClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("usage:canonical") ||
      media_usage_reopened.mediaAssetVisibleInBin(
          QStringLiteral("usage:unused"), QString{}) ||
      media_usage_reopened.mediaAssetVisibleInBin(
          QStringLiteral("usage:duplicate-used"), QString{})) {
    return fail("unused removal and duplicate consolidation should survive save and reopen");
  }

  TimelineDocumentController source_tape;
  source_tape.createTimeline(QStringLiteral("Source Tape"));
  const auto source_tape_can_undo = source_tape.canUndo();
  const QVariantList source_tape_assets{
      QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("tape:first")},
          {QStringLiteral("cameraLabel"), QStringLiteral("Take One")},
          {QStringLiteral("filePath"), still_path},
          {QStringLiteral("thumbnailSource"), QStringLiteral("poster://take-one")},
          {QStringLiteral("mediaType"), QStringLiteral("photo")},
          {QStringLiteral("durationSeconds"), 3.0},
      },
      QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("tape:second")},
          {QStringLiteral("cameraLabel"), QStringLiteral("Take Two")},
          {QStringLiteral("filePath"), still_path},
          {QStringLiteral("thumbnailSource"), QStringLiteral("poster://take-two")},
          {QStringLiteral("mediaType"), QStringLiteral("photo")},
          {QStringLiteral("durationSeconds"), 4.0},
      },
  };
  if (source_tape.configureSourceTape(source_tape_assets) != 2 ||
      !source_tape.sourceTapeEnabled() || source_tape.sourceTapeIndex() != 0 ||
      source_tape.sourceTapeItems().size() != 2 ||
      std::abs(source_tape.sourceTapeDurationSeconds() - 7.0) > 0.0001 ||
      source_tape.sourceClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("tape:first") ||
      source_tape.canUndo() != source_tape_can_undo) {
    return fail("Source Tape should review a filtered online asset list as one identity-preserving reel without editing the sequence");
  }
  source_tape.setSourceInSeconds(0.5);
  source_tape.setSourceOutSeconds(2.25);
  if (!source_tape.stepSourceTape(1) || source_tape.sourceTapeIndex() != 1 ||
      source_tape.sourceClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("tape:second")) {
    return fail("Source Tape should move forward while loading the exact next asset into Source");
  }
  source_tape.setSourceInSeconds(0.25);
  source_tape.setSourceOutSeconds(3.5);
  if (!source_tape.stepSourceTape(-1) ||
      std::abs(source_tape.sourceInSeconds() - 0.5) > 0.0001 ||
      std::abs(source_tape.sourceOutSeconds() - 2.25) > 0.0001 ||
      !source_tape.activateSourceTapeIndex(1) ||
      std::abs(source_tape.sourceInSeconds() - 0.25) > 0.0001 ||
      std::abs(source_tape.sourceOutSeconds() - 3.5) > 0.0001) {
    return fail("Source Tape should preserve independent Source marks when moving between reel items");
  }
  source_tape.clearSourceTape();
  if (source_tape.sourceTapeEnabled() || !source_tape.hasSourceClip() ||
      source_tape.sourceClip().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("tape:second")) {
    return fail("exiting Source Tape should retain the chosen item in Source without losing identity");
  }

  const auto selection_alternate_path =
      workspace.filePath(QStringLiteral("selection-alternate.png"));
  QImage selection_alternate(48, 32, QImage::Format_RGBA8888);
  selection_alternate.fill(QColor(QStringLiteral("#286f96")));
  if (!selection_alternate.save(selection_alternate_path)) {
    return fail("advanced-selection proof should create a distinct source image");
  }
  TimelineDocumentController advanced_selection;
  advanced_selection.createTimeline(QStringLiteral("Advanced Selection"));
  advanced_selection.addClipFromGallery(
      QStringLiteral("asset:matching-source"), still_path,
      QStringLiteral("Matching A"), 3.0, QString{});
  advanced_selection.addClipFromGallery(
      QStringLiteral("asset:matching-source"), still_path,
      QStringLiteral("Matching B"), 3.0, QString{});
  advanced_selection.addVideoTrack();
  advanced_selection.insertClipFromGalleryAtTrack(
      QStringLiteral("asset:different-source"), selection_alternate_path,
      QStringLiteral("Different"), 3.0, 0.0, 3.0, QString{}, 1, 1.0);
  if (advanced_selection.clipCount() != 3) {
    return fail("advanced-selection proof should create two tracks and three clips");
  }

  // Flattened order is Matching A at 0 s, Different at 1 s, then Matching B at 3 s.
  advanced_selection.selectClip(1);
  const auto can_undo_before_direction_selection = advanced_selection.canUndo();
  if (advanced_selection.selectClipsForward(false) != 2 ||
      advanced_selection.selectedClipCount() != 2 ||
      advanced_selection.selectClipsForward(true) != 1 ||
      advanced_selection.selectedClipCount() != 1 ||
      advanced_selection.selectClipsBackward(false) != 2 ||
      advanced_selection.selectedClipCount() != 2 ||
      advanced_selection.selectClipsBackward(true) != 1 ||
      advanced_selection.selectedClipCount() != 1 ||
      advanced_selection.canUndo() != can_undo_before_direction_selection) {
    return fail("forward and backward selection should optionally stay on the reference track without creating an edit");
  }

  advanced_selection.selectClip(0);
  advanced_selection.addSelectedClipEffect(QStringLiteral("Exposure"));
  advanced_selection.selectClip(2);
  advanced_selection.addSelectedClipEffect(QStringLiteral("Exposure"));
  if (!advanced_selection.setMediaAssetColorLabel(
          {QStringLiteral("asset:matching-source")}, QStringLiteral("green"))) {
    return fail("advanced-selection proof should label the repeated source");
  }
  advanced_selection.selectClip(0);
  advanced_selection.toggleClipSelection(2);
  if (!advanced_selection.groupSelectedClips()) {
    return fail("advanced-selection proof should group the two matching clips");
  }
  advanced_selection.selectClip(0);
  const auto similarity_options = advanced_selection.selectedClipSimilarityOptions();
  QSet<QString> available_similarity_keys;
  for (const auto& option_value : similarity_options) {
    const auto option = option_value.toMap();
    if (option.value(QStringLiteral("available")).toBool()) {
      available_similarity_keys.insert(option.value(QStringLiteral("key")).toString());
    }
  }
  const QSet<QString> expected_similarity_keys{
      QStringLiteral("track"), QStringLiteral("media-type"),
      QStringLiteral("label"), QStringLiteral("effect"),
      QStringLiteral("group"), QStringLiteral("source")};
  const auto can_undo_before_similar_selection = advanced_selection.canUndo();
  if (available_similarity_keys != expected_similarity_keys ||
      advanced_selection.selectSimilarClips(
          {QStringLiteral("track"), QStringLiteral("media-type"),
           QStringLiteral("label"), QStringLiteral("effect"),
           QStringLiteral("group"), QStringLiteral("source")}) != 2 ||
      advanced_selection.selectedClipCount() != 2 ||
      advanced_selection.canUndo() != can_undo_before_similar_selection) {
    return fail("Select Similar should compose every available clip constraint and remain selection-only");
  }

  TimelineDocumentController gap_index;
  gap_index.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("Gap Index")},
      {QStringLiteral("frameRate"), 30.0},
  });
  gap_index.insertClipFromGalleryAtTrack(
      QStringLiteral("gap:first"), selection_alternate_path,
      QStringLiteral("First"), 1.0, 0.0, 1.0, QString{}, 0, 0.0);
  gap_index.insertClipFromGalleryAtTrack(
      QStringLiteral("gap:second"), selection_alternate_path,
      QStringLiteral("Second"), 1.0, 0.0, 1.0, QString{}, 0, 2.0);
  gap_index.insertClipFromGalleryAtTrack(
      QStringLiteral("gap:third"), selection_alternate_path,
      QStringLiteral("Third"), 1.0, 0.0, 1.0, QString{}, 0, 5.0);
  gap_index.addVideoTrack();
  gap_index.insertClipFromGalleryAtTrack(
      QStringLiteral("gap:overlay"), selection_alternate_path,
      QStringLiteral("Overlay"), 1.0, 0.0, 1.0, QString{}, 1, 1.0);
  gap_index.seekSequenceSeconds(5.5);
  gap_index.addSequenceMarker(QStringLiteral("After sequence gap"));
  const auto indexed_gaps = gap_index.timelineGaps();
  if (indexed_gaps.size() != 2) {
    return fail("Gap Index should find both track-local and sequence-wide internal gaps");
  }
  const auto track_gap = indexed_gaps.at(0).toMap();
  const auto sequence_gap = indexed_gaps.at(1).toMap();
  if (track_gap.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-gap:v1") ||
      track_gap.value(QStringLiteral("scope")).toString() != QStringLiteral("track") ||
      track_gap.value(QStringLiteral("trackLabel")).toString() != QStringLiteral("V1") ||
      std::abs(track_gap.value(QStringLiteral("durationSeconds")).toDouble() - 1.0) >
          0.0001 ||
      sequence_gap.value(QStringLiteral("scope")).toString() !=
          QStringLiteral("sequence") ||
      std::abs(sequence_gap.value(QStringLiteral("durationSeconds")).toDouble() - 2.0) >
          0.0001 ||
      !track_gap.value(QStringLiteral("safeToClose")).toBool() ||
      !sequence_gap.value(QStringLiteral("safeToClose")).toBool()) {
    return fail("Gap Index rows should identify exact duration, track scope, sequence scope, and safe-close state");
  }
  const auto long_gaps = gap_index.timelineGaps(1.5, 0.0);
  if (long_gaps.size() != 1 ||
      long_gaps.at(0).toMap().value(QStringLiteral("gapId")).toString() !=
          sequence_gap.value(QStringLiteral("gapId")).toString()) {
    return fail("Gap Index duration filters should retain only matching gaps");
  }
  const auto first_gap_preview = gap_index.previewTimelineGap(
      track_gap.value(QStringLiteral("gapId")).toString());
  const auto next_gap = gap_index.navigateTimelineGap(2.0, 1, 0.0, 0.0);
  const auto previous_gap = gap_index.navigateTimelineGap(4.0, -1, 0.0, 0.0);
  if (first_gap_preview.value(QStringLiteral("gapId")).toString() !=
          track_gap.value(QStringLiteral("gapId")).toString() ||
      std::abs(gap_index.sequencePlayheadSeconds() - 4.0) > 0.0001 ||
      next_gap.value(QStringLiteral("gapId")).toString() !=
          sequence_gap.value(QStringLiteral("gapId")).toString() ||
      previous_gap.value(QStringLiteral("gapId")).toString() !=
          sequence_gap.value(QStringLiteral("gapId")).toString()) {
    return fail("Gap Index preview and next/previous navigation should seek the exact gap midpoint");
  }

  gap_index.toggleTrackLocked(0);
  const auto locked_gaps = gap_index.timelineGaps();
  if (locked_gaps.at(0).toMap().value(QStringLiteral("safeToClose")).toBool() ||
      locked_gaps.at(1).toMap().value(QStringLiteral("safeToClose")).toBool() ||
      gap_index.closeAllTimelineGaps() != 0 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:second")) - 2.0) >
          0.0001) {
    std::cerr << "locked gaps=" << locked_gaps.size()
              << " safe0="
              << locked_gaps.at(0).toMap().value(QStringLiteral("safeToClose")).toBool()
              << " safe1="
              << locked_gaps.at(1).toMap().value(QStringLiteral("safeToClose")).toBool()
              << " secondStart="
              << clip_start_by_asset_id(gap_index, QStringLiteral("gap:second")) << '\n';
    return fail("Gap Index should block close operations before moving clips on a locked affected track");
  }
  gap_index.toggleTrackLocked(0);
  if (gap_index.closeTimelineGaps(
          {track_gap.value(QStringLiteral("gapId")).toString()}) != 1 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:second")) - 1.0) >
          0.0001 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:third")) - 4.0) >
          0.0001 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:overlay")) - 1.0) >
          0.0001 ||
      std::abs(gap_index.sequenceMarkers().at(0).toMap()
                   .value(QStringLiteral("seconds")).toDouble() - 5.5) > 0.0001) {
    return fail("closing a selected track gap should move only that track and preserve sequence markers");
  }
  gap_index.undo();
  if (gap_index.timelineGaps().size() != 2 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:third")) - 5.0) >
          0.0001) {
    return fail("closing selected gaps should undo atomically");
  }
  if (gap_index.closeAllTimelineGaps() != 2 || !gap_index.timelineGaps().isEmpty() ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:second")) - 1.0) >
          0.0001 ||
      std::abs(clip_start_by_asset_id(gap_index, QStringLiteral("gap:third")) - 2.0) >
          0.0001 ||
      std::abs(gap_index.sequenceMarkers().at(0).toMap()
                   .value(QStringLiteral("seconds")).toDouble() - 3.5) > 0.0001) {
    return fail("Close All should remove every safe filtered gap while shifting sequence markers only for sequence-wide time");
  }
  gap_index.undo();
  if (gap_index.timelineGaps().size() != 2) {
    return fail("Close All gaps should restore every track and marker in one undo step");
  }
  gap_index.redo();
  if (!gap_index.timelineGaps().isEmpty() || !gap_index.saveTimeline()) {
    return fail("Close All gaps should redo deterministically and save");
  }
  TimelineDocumentController gap_index_reopened;
  if (!gap_index_reopened.loadTimeline(gap_index.savedDocumentPath()) ||
      !gap_index_reopened.timelineGaps().isEmpty() ||
      std::abs(clip_start_by_asset_id(gap_index_reopened, QStringLiteral("gap:third")) - 2.0) >
          0.0001) {
    return fail("closed gap timing should survive save and reopen");
  }

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    return fail("external-audio synchronization proof requires ffmpeg and ffprobe");
  }
  const auto sync_video_path = workspace.filePath(QStringLiteral("sync-camera.mov"));
  const auto sync_wave_path = workspace.filePath(QStringLiteral("sync-recorder.wav"));
  const auto sync_timecode_path = workspace.filePath(QStringLiteral("sync-timecode.wav"));
  if (!run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              QStringLiteral("color=c=purple:s=320x240:d=2:r=30"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              QStringLiteral("aevalsrc=if(between(t\\,0.45\\,1.25)\\,0\\,sin(2*PI*440*t)):s=48000:d=2"),
              QStringLiteral("-shortest"), QStringLiteral("-timecode"),
              QStringLiteral("01:00:00:00"), QStringLiteral("-c:v"),
              QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
              QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
              QStringLiteral("pcm_s16le"), sync_video_path,
          }) ||
      !run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-i"), sync_video_path, QStringLiteral("-vn"),
              QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), sync_wave_path,
          }) ||
      !run_process(
          ffmpeg,
          {
              QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
              QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
              QStringLiteral("sine=frequency=440:sample_rate=48000:duration=1.2"),
              QStringLiteral("-write_bext"), QStringLiteral("1"),
              QStringLiteral("-metadata"),
              QStringLiteral("time_reference=%1").arg(qRound64(3600.5 * 48000.0)),
              QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
              sync_timecode_path,
          })) {
    return fail("external-audio synchronization proof should create camera, waveform, and BWF fixtures");
  }

  TimelineDocumentController technical_metadata;
  const auto inspected_media = technical_metadata.inspectMediaAssetTechnicalMetadata(
      QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("metadata:camera")},
          {QStringLiteral("filePath"), sync_video_path},
          {QStringLiteral("cameraLabel"), QStringLiteral("Camera A")},
          {QStringLiteral("lensModel"), QStringLiteral("35 mm")},
          {QStringLiteral("captureDate"), QStringLiteral("2026-07-19")},
      });
  if (inspected_media.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-media-technical-metadata:v1") ||
      !inspected_media.value(QStringLiteral("available")).toBool() ||
      inspected_media.value(QStringLiteral("videoCodec")).toString() !=
          QStringLiteral("h264") ||
      inspected_media.value(QStringLiteral("width")).toInt() != 320 ||
      inspected_media.value(QStringLiteral("height")).toInt() != 240 ||
      std::abs(inspected_media.value(QStringLiteral("frameRate")).toDouble() - 30.0) > 0.01 ||
      !inspected_media.value(QStringLiteral("hasAudio")).toBool() ||
      inspected_media.value(QStringLiteral("audioChannels")).toInt() < 1 ||
      inspected_media.value(QStringLiteral("camera")).toString() !=
          QStringLiteral("Camera A") ||
      inspected_media.value(QStringLiteral("captureDate")).toString() !=
          QStringLiteral("2026-07-19")) {
    return fail("contextual metadata inspection should publish real codec, resolution, frame rate, audio, and camera/date fields from encoded media");
  }

  TimelineDocumentController hover_scrub;
  QElapsedTimer hover_decode_timer;
  hover_decode_timer.start();
  const auto hover_frame_source = wait_for_media_hover_frame(
      &hover_scrub, QStringLiteral("video:hover-scrub"), sync_video_path, 1.0);
  const auto hover_frame_path = QUrl(hover_frame_source).toLocalFile();
  const QImage hover_frame(hover_frame_path);
  if (hover_frame_source.isEmpty() || hover_frame.isNull() ||
      hover_frame.width() > 384 || hover_frame.height() > 216 ||
      hover_decode_timer.elapsed() > 10000) {
    return fail("hover scrubbing should decode one bounded low-resolution frame asynchronously");
  }
  QElapsedTimer cached_hover_timer;
  cached_hover_timer.start();
  const auto cached_hover_source = hover_scrub.requestMediaHoverFrame(
      QStringLiteral("video:hover-scrub"), sync_video_path, 1.0);
  if (cached_hover_source != hover_frame_source || cached_hover_timer.elapsed() > 50) {
    return fail("revisiting a scrub sample should return the bounded cache immediately without another decode");
  }

  TimelineDocumentController paste_attributes;
  paste_attributes.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("Paste Attributes")},
      {QStringLiteral("frameRate"), 30.0},
  });
  paste_attributes.insertClipFromGalleryAtTrack(
      QStringLiteral("video:attribute-source"), sync_video_path,
      QStringLiteral("Styled Source"), 2.0, 0.0, 1.0, QString{}, 0, 0.0);
  paste_attributes.insertClipFromGalleryAtTrack(
      QStringLiteral("video:attribute-target"), sync_video_path,
      QStringLiteral("Plain Target"), 2.0, 0.25, 1.25, QString{}, 0, 1.0);
  paste_attributes.insertClipFromGalleryAtTrack(
      QStringLiteral("video:attribute-tail"), sync_video_path,
      QStringLiteral("Tail Handle"), 2.0, 0.25, 1.25, QString{}, 0, 2.0);
  if (!wait_for_media_analysis(&paste_attributes)) {
    return fail("Paste Attributes proof should resolve source audio and video metadata");
  }
  paste_attributes.selectClip(0);
  paste_attributes.addSelectedClipEffect(QStringLiteral("Exposure"));
  paste_attributes.setSelectedClipEffectAmount(QStringLiteral("Exposure"), 0.65);
  paste_attributes.setSelectedFrameScale(1.4);
  paste_attributes.setSelectedFrameOffsetX(0.18);
  paste_attributes.setSelectedFrameOffsetY(-0.12);
  paste_attributes.setSelectedFrameRotationDegrees(6.0);
  paste_attributes.setSelectedClipOpacity(0.82);
  paste_attributes.setSelectedFrameMode(QStringLiteral("fill"));
  paste_attributes.setSelectedClipPlaybackRate(2.0);
  paste_attributes.setSelectedClipAudioGain(-4.5);
  paste_attributes.setSelectedClipFadeInSeconds(0.15);
  paste_attributes.setSelectedClipFadeOutSeconds(0.25);
  paste_attributes.assignSelectedClipTransition(
      QStringLiteral("out"), QStringLiteral("Cross Dissolve"), 0.2);
  paste_attributes.seekSequenceSeconds(0.0);
  if (!paste_attributes.toggleSelectedClipKeyframe(QStringLiteral("frameScale")) ||
      paste_attributes.copySelectedClips() != 1) {
    return fail("Paste Attributes proof should copy a fully styled source clip");
  }
  paste_attributes.selectClip(1);
  const auto attribute_comparison = paste_attributes.pasteAttributesComparison();
  const auto attribute_categories =
      attribute_comparison.value(QStringLiteral("categories")).toList();
  QSet<QString> available_attribute_categories;
  auto changed_attribute_count = 0;
  for (const auto& category_value : attribute_categories) {
    const auto category = category_value.toMap();
    if (category.value(QStringLiteral("available")).toBool()) {
      available_attribute_categories.insert(category.value(QStringLiteral("key")).toString());
    }
    changed_attribute_count += category.value(QStringLiteral("changed")).toBool() ? 1 : 0;
  }
  const QSet<QString> expected_attribute_categories{
      QStringLiteral("effects"), QStringLiteral("motion"), QStringLiteral("crop"),
      QStringLiteral("speed"), QStringLiteral("audio"), QStringLiteral("transitions"),
      QStringLiteral("keyframes")};
  if (!attribute_comparison.value(QStringLiteral("available")).toBool() ||
      attribute_comparison.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-paste-attributes:v1") ||
      attribute_comparison.value(QStringLiteral("sourceLabel")).toString() !=
          QStringLiteral("Styled Source") ||
      attribute_comparison.value(QStringLiteral("targetCount")).toInt() != 1 ||
      available_attribute_categories != expected_attribute_categories ||
      changed_attribute_count != 7) {
    std::cerr << "paste comparison available="
              << attribute_comparison.value(QStringLiteral("available")).toBool()
              << " source="
              << attribute_comparison.value(QStringLiteral("sourceLabel")).toString().toStdString()
              << " targetCount="
              << attribute_comparison.value(QStringLiteral("targetCount")).toInt()
              << " categories=" << available_attribute_categories.size()
              << " changed=" << changed_attribute_count << '\n';
    for (const auto& category_value : attribute_categories) {
      const auto category = category_value.toMap();
      std::cerr << "  "
                << category.value(QStringLiteral("key")).toString().toStdString()
                << " available="
                << category.value(QStringLiteral("available")).toBool()
                << " changed=" << category.value(QStringLiteral("changed")).toBool()
                << " source="
                << category.value(QStringLiteral("sourceValue")).toString().toStdString()
                << " destination="
                << category.value(QStringLiteral("destinationValue")).toString().toStdString()
                << '\n';
    }
    return fail("Paste Attributes comparison should expose all seven independent source/destination categories");
  }

  if (paste_attributes.pasteAttributesToSelectedClips(
          {QStringLiteral("effects")}) != 1) {
    return fail("Paste Attributes should apply one selected category");
  }
  const auto effect_only_target = paste_attributes.currentClip();
  if (effect_only_target.value(QStringLiteral("effectStack")).toList().size() != 1 ||
      std::abs(effect_only_target.value(QStringLiteral("baseFrameScale")).toDouble() - 1.0) >
          0.0001 ||
      effect_only_target.value(QStringLiteral("frameMode")).toString() !=
          QStringLiteral("fit") ||
      std::abs(effect_only_target.value(QStringLiteral("playbackRate")).toDouble() - 1.0) >
          0.0001) {
    return fail("Paste Attributes category choices should not modify unselected properties");
  }
  paste_attributes.undo();
  if (!paste_attributes.currentClip().value(QStringLiteral("effectStack")).toList().isEmpty()) {
    return fail("a selective Paste Attributes command should undo in one edit");
  }

  const QStringList every_attribute_category{
      QStringLiteral("effects"), QStringLiteral("motion"), QStringLiteral("crop"),
      QStringLiteral("speed"), QStringLiteral("audio"), QStringLiteral("transitions"),
      QStringLiteral("keyframes")};
  if (paste_attributes.pasteAttributesToSelectedClips(every_attribute_category) != 1) {
    return fail("Paste Attributes should apply every selected category atomically");
  }
  const auto fully_styled_target = paste_attributes.currentClip();
  if (fully_styled_target.value(QStringLiteral("effectStack")).toList().size() != 1 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseFrameScale")).toDouble() - 1.4) >
          0.0001 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseFrameOffsetX")).toDouble() - 0.18) >
          0.0001 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseFrameOffsetY")).toDouble() + 0.12) >
          0.0001 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseFrameRotationDegrees")).toDouble() - 6.0) >
          0.0001 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseOpacity")).toDouble() - 0.82) >
          0.0001 ||
      fully_styled_target.value(QStringLiteral("frameMode")).toString() !=
          QStringLiteral("fill") ||
      std::abs(fully_styled_target.value(QStringLiteral("playbackRate")).toDouble() - 2.0) >
          0.0001 ||
      std::abs(fully_styled_target.value(QStringLiteral("baseAudioGainDb")).toDouble() + 4.5) >
          0.0001 ||
      !fully_styled_target.value(QStringLiteral("transitionInName")).toString().isEmpty() ||
      fully_styled_target.value(QStringLiteral("transitionOutName")).toString() !=
          QStringLiteral("Cross Dissolve") ||
      fully_styled_target.value(QStringLiteral("keyframes")).toList().size() != 1) {
    return fail("Paste Attributes should faithfully apply effects, motion, crop, speed, audio, transitions, and scaled keyframes");
  }
  paste_attributes.selectClip(0);
  const auto prior_transition_source = paste_attributes.currentClip();
  paste_attributes.selectClip(2);
  const auto paired_transition_tail = paste_attributes.currentClip();
  if (!prior_transition_source.value(QStringLiteral("transitionOutName")).toString().isEmpty() ||
      paired_transition_tail.value(QStringLiteral("transitionInName")).toString() !=
          QStringLiteral("Cross Dissolve")) {
    return fail("pasted Cross Dissolves should clear the old pair and create only a valid adjacent handled pair");
  }
  paste_attributes.selectClip(1);
  paste_attributes.undo();
  if (!paste_attributes.currentClip().value(QStringLiteral("effectStack")).toList().isEmpty() ||
      std::abs(paste_attributes.currentClip()
                   .value(QStringLiteral("playbackRate")).toDouble() - 1.0) > 0.0001) {
    return fail("full Paste Attributes should undo every category and downstream timing together");
  }
  paste_attributes.redo();
  if (!paste_attributes.saveTimeline()) {
    return fail("Paste Attributes proof should save the styled destination");
  }
  TimelineDocumentController paste_attributes_reopened;
  if (!paste_attributes_reopened.loadTimeline(paste_attributes.savedDocumentPath())) {
    return fail("Paste Attributes proof should reopen the saved sequence");
  }
  paste_attributes_reopened.selectClip(1);
  const auto reopened_styled_target = paste_attributes_reopened.currentClip();
  if (reopened_styled_target.value(QStringLiteral("effectStack")).toList().size() != 1 ||
      reopened_styled_target.value(QStringLiteral("frameMode")).toString() !=
          QStringLiteral("fill") ||
      std::abs(reopened_styled_target.value(QStringLiteral("playbackRate")).toDouble() - 2.0) >
          0.0001 ||
      reopened_styled_target.value(QStringLiteral("keyframes")).toList().size() != 1) {
    return fail("pasted attributes should survive save and reopen");
  }

  TimelineDocumentController edit_points;
  edit_points.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("Edit Point Selection")},
      {QStringLiteral("frameRate"), 30.0},
  });
  edit_points.addClipFromGallery(
      QStringLiteral("video:edit-out"), sync_video_path,
      QStringLiteral("Outgoing Camera"), 2.0, QString{});
  edit_points.selectClip(0);
  edit_points.trimSelectedClipStartToSequenceSeconds(0.25, false);
  edit_points.moveSelectedClipToTrackRow(0, 0.0);
  edit_points.trimSelectedClipEndToSequenceSeconds(1.0, false);
  edit_points.addClipFromGallery(
      QStringLiteral("video:edit-in"), sync_video_path,
      QStringLiteral("Incoming Camera"), 2.0, QString{});
  edit_points.selectClip(1);
  edit_points.trimSelectedClipStartToSequenceSeconds(1.25, false);
  edit_points.moveSelectedClipToTrackRow(0, 1.0);
  edit_points.trimSelectedClipEndToSequenceSeconds(2.0, false);
  const auto incoming_edit = edit_points.selectedEditPointInfo(QStringLiteral("start"));
  if (incoming_edit.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-edit-point:v1") ||
      !incoming_edit.value(QStringLiteral("hasAdjacentCut")).toBool() ||
      incoming_edit.value(QStringLiteral("outgoingLabel")).toString() !=
          QStringLiteral("Outgoing Camera") ||
      incoming_edit.value(QStringLiteral("incomingLabel")).toString() !=
          QStringLiteral("Incoming Camera") ||
      incoming_edit.value(QStringLiteral("boundaryFrame")).toLongLong() != 30 ||
      std::abs(incoming_edit.value(QStringLiteral("outgoingTailHandleSeconds")).toDouble() -
               0.75) > 0.001 ||
      std::abs(incoming_edit.value(QStringLiteral("incomingHeadHandleSeconds")).toDouble() -
               0.25) > 0.001 ||
      std::abs(incoming_edit.value(QStringLiteral("transitionRoomSeconds")).toDouble() -
               0.25) > 0.001) {
    return fail("edit-point selection should expose exact boundary time, both sources, source limits, and shared transition room");
  }
  const auto outgoing_edit = edit_points.selectOtherEditPointSide(QStringLiteral("start"));
  if (outgoing_edit.value(QStringLiteral("edge")).toString() != QStringLiteral("end") ||
      outgoing_edit.value(QStringLiteral("boundaryFrame")).toLongLong() != 30 ||
      edit_points.currentClip().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Outgoing Camera")) {
    return fail("edit-point mode should switch between outgoing and incoming sides without moving the boundary");
  }
  const auto next_edit = edit_points.selectAdjacentEditPoint(0.0, 1);
  const auto previous_edit = edit_points.selectAdjacentEditPoint(2.0, -1);
  if (next_edit.value(QStringLiteral("edge")).toString() != QStringLiteral("start") ||
      previous_edit.value(QStringLiteral("edge")).toString() != QStringLiteral("end") ||
      next_edit.value(QStringLiteral("boundaryFrame")).toLongLong() != 30 ||
      previous_edit.value(QStringLiteral("boundaryFrame")).toLongLong() != 30) {
    return fail("previous and next edit navigation should select exact adjacent cut sides deterministically");
  }

  TimelineDocumentController external_sync;
  external_sync.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("External Sync")},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("audioSampleRate"), 48000},
  });
  external_sync.addClipFromGallery(
      QStringLiteral("video:sync-camera"), sync_video_path,
      QStringLiteral("Camera Reference"), 0.0, QString{});
  if (!wait_for_media_analysis(&external_sync)) {
    return fail("external-audio sync should resolve camera metadata");
  }
  external_sync.loadSourceClipFromGallery(
      QStringLiteral("audio:sync-wave"), sync_wave_path,
      QStringLiteral("Recorder WAV"), 0.0, QString{});
  if (!wait_for_media_analysis(&external_sync)) {
    return fail("external-audio sync should resolve recorder metadata");
  }
  external_sync.appendSourceClipToTimeline();
  external_sync.selectClip(0);
  external_sync.toggleClipSelection(1);
  const auto waveform_preview =
      external_sync.previewSelectedExternalAudioSync(QStringLiteral("waveform"), 0.0);
  const auto external_audio_clip_id =
      waveform_preview.value(QStringLiteral("audioClipId")).toString();
  const auto original_audio_start =
      waveform_preview.value(QStringLiteral("audioCurrentStartSeconds")).toDouble();
  if (!waveform_preview.value(QStringLiteral("valid")).toBool() ||
      waveform_preview.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-external-audio-sync:v1") ||
      waveform_preview.value(QStringLiteral("sampleRate")).toInt() != 48000 ||
      waveform_preview.value(QStringLiteral("sampleOffset")).toLongLong() !=
          qRound64(-original_audio_start * 48000.0) ||
      std::abs(waveform_preview.value(QStringLiteral("audioTargetStartSeconds")).toDouble()) >
          1.0 / 48000.0 ||
      std::abs(clip_start_by_id(external_sync, external_audio_clip_id) -
               original_audio_start) > 0.000001) {
    std::cerr << "waveform preview valid="
              << waveform_preview.value(QStringLiteral("valid")).toBool()
              << " explanation="
              << waveform_preview.value(QStringLiteral("explanation")).toString().toStdString()
              << " current=" << original_audio_start
              << " target="
              << waveform_preview.value(QStringLiteral("audioTargetStartSeconds")).toDouble()
              << " samples="
              << waveform_preview.value(QStringLiteral("sampleOffset")).toLongLong()
              << " confidence="
              << waveform_preview.value(QStringLiteral("confidence")).toDouble()
              << '\n';
    return fail("waveform sync preview should be sample-exact, confidence-bearing, and non-mutating");
  }
  if (!external_sync.applySelectedExternalAudioSync(QStringLiteral("waveform"), 0.0) ||
      std::abs(clip_start_by_id(external_sync, external_audio_clip_id)) >
          1.0 / 48000.0) {
    return fail("waveform sync should apply exact recorder placement");
  }
  external_sync.undo();
  if (std::abs(clip_start_by_id(external_sync, external_audio_clip_id) -
               original_audio_start) > 0.000001) {
    return fail("external-audio sync should undo as one edit");
  }
  external_sync.redo();
  if (!external_sync.saveTimeline()) {
    return fail("external-audio sync should save");
  }
  TimelineDocumentController external_sync_reopened;
  if (!external_sync_reopened.loadTimeline(external_sync.savedDocumentPath()) ||
      std::abs(clip_start_by_id(external_sync_reopened, external_audio_clip_id)) >
          1.0 / 48000.0) {
    return fail("external-audio sync should survive save and reopen");
  }

  TimelineDocumentController timecode_sync;
  timecode_sync.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("BWF Timecode Sync")},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("audioSampleRate"), 48000},
  });
  timecode_sync.addClipFromGallery(
      QStringLiteral("video:sync-timecode"), sync_video_path,
      QStringLiteral("Camera TC"), 0.0, QString{});
  if (!wait_for_media_analysis(&timecode_sync)) {
    return fail("timecode sync should resolve camera metadata");
  }
  timecode_sync.loadSourceClipFromGallery(
      QStringLiteral("audio:sync-timecode"), sync_timecode_path,
      QStringLiteral("BWF Recorder"), 0.0, QString{});
  if (!wait_for_media_analysis(&timecode_sync)) {
    return fail("timecode sync should resolve BWF metadata");
  }
  timecode_sync.appendSourceClipToTimeline();
  timecode_sync.selectClip(0);
  timecode_sync.toggleClipSelection(1);
  const auto timecode_preview =
      timecode_sync.previewSelectedExternalAudioSync(QStringLiteral("timecode"), 0.0);
  if (!timecode_preview.value(QStringLiteral("valid")).toBool() ||
      std::abs(timecode_preview.value(QStringLiteral("audioTargetStartSeconds")).toDouble() -
               0.5) > 1.0 / 48000.0) {
    return fail("Broadcast Wave time_reference should preserve the embedded half-second offset");
  }
  const auto manual_preview =
      timecode_sync.previewSelectedExternalAudioSync(QStringLiteral("manual"), 0.125);
  if (!manual_preview.value(QStringLiteral("valid")).toBool() ||
      std::abs(manual_preview.value(QStringLiteral("offsetSeconds")).toDouble() - 0.125) >
          1.0 / 48000.0) {
    return fail("manual sync should preview a sample-rounded fine offset");
  }

  const auto transcript_fixture_path =
      workspace.filePath(QStringLiteral("transcript-speakers.json"));
  QFile transcript_fixture(transcript_fixture_path);
  const QJsonArray transcript_words = {
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Welcome")},
                  {QStringLiteral("startSeconds"), 0.10},
                  {QStringLiteral("endSeconds"), 0.28},
                  {QStringLiteral("speaker"), QStringLiteral("Host")},
                  {QStringLiteral("confidence"), 0.96}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("to")},
                  {QStringLiteral("startSeconds"), 0.30},
                  {QStringLiteral("endSeconds"), 0.38},
                  {QStringLiteral("speaker"), QStringLiteral("Host")},
                  {QStringLiteral("confidence"), 0.92}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("our")},
                  {QStringLiteral("startSeconds"), 0.40},
                  {QStringLiteral("endSeconds"), 0.49},
                  {QStringLiteral("speaker"), QStringLiteral("Host")},
                  {QStringLiteral("confidence"), 0.90}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("show.")},
                  {QStringLiteral("startSeconds"), 0.51},
                  {QStringLiteral("endSeconds"), 0.66},
                  {QStringLiteral("speaker"), QStringLiteral("Host")},
                  {QStringLiteral("confidence"), 0.94}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Thanks")},
                  {QStringLiteral("startSeconds"), 0.82},
                  {QStringLiteral("endSeconds"), 0.98},
                  {QStringLiteral("speaker"), QStringLiteral("Guest")},
                  {QStringLiteral("confidence"), 0.88}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("for")},
                  {QStringLiteral("startSeconds"), 1.00},
                  {QStringLiteral("endSeconds"), 1.08},
                  {QStringLiteral("speaker"), QStringLiteral("Guest")},
                  {QStringLiteral("confidence"), 0.86}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("having")},
                  {QStringLiteral("startSeconds"), 1.10},
                  {QStringLiteral("endSeconds"), 1.24},
                  {QStringLiteral("speaker"), QStringLiteral("Guest")},
                  {QStringLiteral("confidence"), 0.84}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("me.")},
                  {QStringLiteral("startSeconds"), 1.26},
                  {QStringLiteral("endSeconds"), 1.42},
                  {QStringLiteral("speaker"), QStringLiteral("Guest")},
                  {QStringLiteral("confidence"), 0.82}},
  };
  if (!transcript_fixture.open(QIODevice::WriteOnly) ||
      transcript_fixture.write(
          QJsonDocument(QJsonObject{{QStringLiteral("words"), transcript_words}})
              .toJson(QJsonDocument::Compact)) <= 0) {
    return fail("caption workflow proof should write its speaker/confidence fixture");
  }
  transcript_fixture.close();
  qputenv(
      "PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE",
      transcript_fixture_path.toUtf8());

  TimelineDocumentController caption_workflow;
  caption_workflow.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("Caption Workflow")},
      {QStringLiteral("frameRate"), 30.0},
  });
  caption_workflow.addClipFromGallery(
      QStringLiteral("video:caption-workflow"), sync_video_path,
      QStringLiteral("Interview"), 0.0, QString{});
  if (!wait_for_media_analysis(&caption_workflow)) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption workflow should resolve interview audio metadata");
  }
  caption_workflow.setTranscriptionOptions(QStringLiteral("es-MX"), true);
  if (caption_workflow.transcriptionLanguage() != QStringLiteral("es-MX") ||
      !caption_workflow.transcriptionSpeakerDiarization() ||
      caption_workflow.transcriptionProcessingDisclosure().isEmpty() ||
      caption_workflow.transcribeTimelineAudio() != 1 ||
      !wait_for_transcription(&caption_workflow)) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("transcription should honor language, diarization, processing disclosure, and completion");
  }
  auto transcript_segments = caption_workflow.transcriptSegments();
  if (transcript_segments.size() != 2 ||
      transcript_segments.first().toMap().value(QStringLiteral("speaker")).toString() !=
          QStringLiteral("Host") ||
      transcript_segments.last().toMap().value(QStringLiteral("speaker")).toString() !=
          QStringLiteral("Guest") ||
      transcript_segments.first().toMap().value(QStringLiteral("confidence")).toDouble() < 0.90) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("transcript segments should retain speaker labels and confidence summaries");
  }
  if (caption_workflow.retryTranscription() != 1 ||
      !wait_for_transcription(&caption_workflow)) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("failed or completed transcription should expose one deterministic retry path");
  }
  transcript_segments = caption_workflow.transcriptSegments();
  const auto first_segment = transcript_segments.first().toMap();
  const auto first_segment_id = first_segment.value(QStringLiteral("segmentId")).toString();
  if (!caption_workflow.seekTranscriptWord(first_segment_id, 0, QStringLiteral("program")) ||
      std::abs(caption_workflow.sequencePlayheadSeconds() - 0.10) > 0.01 ||
      !caption_workflow.seekTranscriptWord(first_segment_id, 0, QStringLiteral("source")) ||
      caption_workflow.sourceClip().value(QStringLiteral("displayLabel")).toString() !=
          QStringLiteral("Interview") ||
      !caption_workflow.updateTranscriptWord(
          first_segment_id, 0, QStringLiteral("Hello"), QStringLiteral("Presenter"))) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("word navigation should synchronize Source/Program and corrections should update speaker/text");
  }
  const auto corrected_segment = caption_workflow.transcriptSegments().first().toMap();
  const auto corrected_word =
      corrected_segment.value(QStringLiteral("words")).toList().first().toMap();
  if (corrected_word.value(QStringLiteral("text")).toString() != QStringLiteral("Hello") ||
      corrected_word.value(QStringLiteral("speaker")).toString() !=
          QStringLiteral("Presenter")) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("transcript corrections should publish immediately without losing timing");
  }
  const QVariantMap segmentation_options{
      {QStringLiteral("maxCharsPerLine"), 18},
      {QStringLiteral("maxLines"), 2},
      {QStringLiteral("maxReadingSpeedCps"), 18.0},
      {QStringLiteral("minimumDurationSeconds"), 0.6},
      {QStringLiteral("minimumGapFrames"), 2},
      {QStringLiteral("semanticLineBreaks"), true},
  };
  const auto segmentation_preview =
      caption_workflow.previewCaptionSegmentation(segmentation_options);
  const auto preview_cues = segmentation_preview.value(QStringLiteral("cues")).toList();
  if (!segmentation_preview.value(QStringLiteral("valid")).toBool() ||
      preview_cues.size() < 2 ||
      preview_cues.first().toMap().value(QStringLiteral("endSeconds")).toDouble() >=
          preview_cues.at(1).toMap().value(QStringLiteral("startSeconds")).toDouble() ||
      !caption_workflow.applyCaptionSegmentation(segmentation_options)) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption segmentation should preview semantic, readable, collision-free cues before one apply");
  }
  const auto validation = caption_workflow.validateCaptions(QStringLiteral("srt"));
  if (validation.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-caption-validation:v1") ||
      validation.value(QStringLiteral("cueCount")).toInt() < 2 ||
      validation.value(QStringLiteral("issues")).toList().isEmpty()) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption validation should report a structured scan instead of a pass/fail placeholder");
  }
  const auto caption_delivery = caption_workflow.timelineDeliveryPreflight();
  auto caption_validation_in_preflight = false;
  for (const auto& value : caption_delivery.value(QStringLiteral("issues")).toList()) {
    if (value.toMap().value(QStringLiteral("key")).toString() ==
        QStringLiteral("caption-validation")) {
      caption_validation_in_preflight = true;
      break;
    }
  }
  if (!caption_validation_in_preflight) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("delivery preflight should surface the structured caption validator before publish");
  }
  caption_workflow.undo();
  if (caption_workflow.transcriptSegments().first().toMap()
          .value(QStringLiteral("words")).toList().first().toMap()
          .value(QStringLiteral("text")).toString() != QStringLiteral("Hello")) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption segmentation should undo without losing transcript corrections");
  }
  caption_workflow.redo();
  if (!caption_workflow.saveTimeline()) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption workflow should save");
  }
  TimelineDocumentController caption_workflow_reopened;
  if (!caption_workflow_reopened.loadTimeline(caption_workflow.savedDocumentPath()) ||
      caption_workflow_reopened.transcriptionLanguage() != QStringLiteral("es-MX") ||
      caption_workflow_reopened.transcriptSegments().isEmpty()) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("caption words, speaker metadata, segmentation, and transcription options should survive reopen");
  }

  const auto diarization_audio_path = workspace.filePath(QStringLiteral("diarization.wav"));
  const auto diarization_video_path = workspace.filePath(QStringLiteral("diarization.mp4"));
  if (!run_process(
          ffmpeg,
          {QStringLiteral("-y"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("sine=frequency=150:sample_rate=48000:duration=0.75"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("anullsrc=r=48000:cl=mono:d=0.30"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("sine=frequency=520:sample_rate=48000:duration=0.75"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("anullsrc=r=48000:cl=mono:d=0.30"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("sine=frequency=150:sample_rate=48000:duration=0.75"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("anullsrc=r=48000:cl=mono:d=0.30"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("sine=frequency=520:sample_rate=48000:duration=0.75"),
           QStringLiteral("-filter_complex"),
           QStringLiteral("[0:a][1:a][2:a][3:a][4:a][5:a][6:a]concat=n=7:v=0:a=1[out]"),
           QStringLiteral("-map"), QStringLiteral("[out]"),
           QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
           diarization_audio_path}) ||
      !run_process(
          ffmpeg,
          {QStringLiteral("-y"),
           QStringLiteral("-f"), QStringLiteral("lavfi"),
           QStringLiteral("-i"), QStringLiteral("color=c=0x202020:s=320x240:r=30:d=3.9"),
           QStringLiteral("-i"), diarization_audio_path,
           QStringLiteral("-shortest"),
           QStringLiteral("-c:v"), QStringLiteral("libx264"),
           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
           QStringLiteral("-c:a"), QStringLiteral("aac"),
           diarization_video_path})) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("speaker-diarization proof should create two repeated acoustic identities");
  }
  const auto diarization_fixture_path =
      workspace.filePath(QStringLiteral("diarization-transcript.json"));
  QFile diarization_fixture(diarization_fixture_path);
  const QJsonArray diarization_words{
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Alpha.")},
                  {QStringLiteral("startSeconds"), 0.10},
                  {QStringLiteral("endSeconds"), 0.55},
                  {QStringLiteral("confidence"), 0.96}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Bravo.")},
                  {QStringLiteral("startSeconds"), 1.15},
                  {QStringLiteral("endSeconds"), 1.60},
                  {QStringLiteral("confidence"), 0.94}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Again.")},
                  {QStringLiteral("startSeconds"), 2.20},
                  {QStringLiteral("endSeconds"), 2.65},
                  {QStringLiteral("confidence"), 0.92}},
      QJsonObject{{QStringLiteral("text"), QStringLiteral("Return.")},
                  {QStringLiteral("startSeconds"), 3.25},
                  {QStringLiteral("endSeconds"), 3.70},
                  {QStringLiteral("confidence"), 0.90}},
  };
  if (!diarization_fixture.open(QIODevice::WriteOnly) ||
      diarization_fixture.write(
          QJsonDocument(QJsonObject{{QStringLiteral("words"), diarization_words}})
              .toJson(QJsonDocument::Compact)) <= 0) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("speaker-diarization proof should write unlabelled recognition words");
  }
  diarization_fixture.close();
  qputenv(
      "PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE",
      diarization_fixture_path.toUtf8());
  TimelineDocumentController local_diarization;
  local_diarization.createTimeline(QStringLiteral("Local Speaker Identification"));
  local_diarization.addClipFromGallery(
      QStringLiteral("video:local-diarization"), diarization_video_path,
      QStringLiteral("Two Acoustic Identities"), 0.0, QString{});
  if (!wait_for_media_analysis(&local_diarization) ||
      local_diarization.transcribeTimelineAudio() != 1 ||
      !wait_for_transcription(&local_diarization)) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("speaker diarization should complete after recognition without blocking the UI thread");
  }
  const auto diarized_segments = local_diarization.transcriptSegments();
  if (diarized_segments.size() != 4) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("speaker changes should preserve four independently timed utterances");
  }
  const auto first_speaker =
      diarized_segments.at(0).toMap().value(QStringLiteral("speaker")).toString();
  const auto second_speaker =
      diarized_segments.at(1).toMap().value(QStringLiteral("speaker")).toString();
  if (first_speaker.isEmpty() || second_speaker.isEmpty() ||
      first_speaker == second_speaker ||
      diarized_segments.at(2).toMap().value(QStringLiteral("speaker")).toString() !=
          first_speaker ||
      diarized_segments.at(3).toMap().value(QStringLiteral("speaker")).toString() !=
          second_speaker ||
      !local_diarization.transcriptionStatus().contains(
          QStringLiteral("Identified 2 speakers locally")) ||
      !local_diarization.transcriptionProcessingDisclosure().contains(
          QStringLiteral("Speaker identification always runs locally"))) {
    qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");
    return fail("local diarization should cluster repeated acoustic identities into stable disclosed speaker labels");
  }
  qunsetenv("PHOTO_EDITOR_TIMELINE_TRANSCRIPTION_FIXTURE");

  const auto deterministic_source_path =
      workspace.filePath(QStringLiteral("deterministic-source.png"));
  QImage deterministic_source(48, 48, QImage::Format_RGB32);
  deterministic_source.fill(QColor(28, 96, 184));
  if (!deterministic_source.save(deterministic_source_path)) {
    return fail("determinism proof should create its representative source image");
  }
  TimelineDocumentController deterministic;
  deterministic.createTimelineWithSettings(QVariantMap{
      {QStringLiteral("timelineName"), QStringLiteral("Deterministic Sequence")},
      {QStringLiteral("width"), 64},
      {QStringLiteral("height"), 64},
      {QStringLiteral("frameRate"), 24.0},
  });
  deterministic.addClipFromGallery(
      QStringLiteral("photo:deterministic"),
      deterministic_source_path,
      QStringLiteral("Deterministic Frame"),
      0.5,
      QString{});
  deterministic.addSequenceMarker(QStringLiteral("Hash anchor"));
  if (!deterministic.saveTimeline()) {
    return fail("determinism proof should save its representative sequence");
  }
  const auto first_document_hash = deterministic.deterministicDocumentHash();
  TimelineDocumentController deterministic_reopened;
  const auto reopened_loaded = deterministic_reopened.loadTimeline(deterministic.savedDocumentPath());
  const auto reopened_hash_before_save = deterministic_reopened.deterministicDocumentHash();
  const auto reopened_saved_match = deterministic_reopened
                                        .timelineDeterminismFingerprint()
                                        .value(QStringLiteral("savedDocumentMatches"))
                                        .toBool();
  const auto reopened_saved = deterministic_reopened.saveTimeline();
  if (!reopened_loaded || reopened_hash_before_save != first_document_hash ||
      !reopened_saved_match || !reopened_saved ||
      deterministic_reopened.deterministicDocumentHash() != first_document_hash) {
    return fail("save, reopen, and repeated save should retain one canonical editorial-state hash");
  }
  const auto first_render_path = workspace.filePath(QStringLiteral("deterministic-a.mp4"));
  const auto second_render_path = workspace.filePath(QStringLiteral("deterministic-b.mp4"));
  if (!deterministic_reopened.renderCurrentTimeline(first_render_path) ||
      !deterministic_reopened.renderCurrentTimeline(second_render_path)) {
    return fail("determinism proof should render the representative sequence twice");
  }
  const auto first_fingerprint =
      deterministic_reopened.timelineDeterminismFingerprint(first_render_path);
  const auto second_fingerprint =
      deterministic_reopened.timelineDeterminismFingerprint(second_render_path);
  if (!first_fingerprint.value(QStringLiteral("valid")).toBool() ||
      !second_fingerprint.value(QStringLiteral("valid")).toBool() ||
      first_fingerprint.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("timeline-determinism:v1") ||
      first_fingerprint.value(QStringLiteral("documentHash")) !=
          second_fingerprint.value(QStringLiteral("documentHash")) ||
      first_fingerprint.value(QStringLiteral("videoFrameHash")).toString().isEmpty() ||
      first_fingerprint.value(QStringLiteral("videoFrameHash")) !=
          second_fingerprint.value(QStringLiteral("videoFrameHash")) ||
      first_fingerprint.value(QStringLiteral("combinedHash")) !=
          second_fingerprint.value(QStringLiteral("combinedHash"))) {
    return fail("two representative renders should produce identical normalized frame and combined hashes");
  }
  const auto encoder_recovery_path =
      workspace.filePath(QStringLiteral("encoder-device-loss-recovered.mp4"));
  if (!deterministic_reopened.setDeliverySetting(
          QStringLiteral("encoderMode"), QStringLiteral("hardware"))) {
    return fail("encoder recovery proof should request the hardware delivery path");
  }
  const auto encoder_recovery_hash =
      deterministic_reopened.deterministicDocumentHash();
  qputenv(
      "PHOTO_EDITOR_TIMELINE_FORCE_HARDWARE_ENCODER_FAILURE",
      QByteArrayLiteral("1"));
  const auto encoder_recovered =
      deterministic_reopened.renderCurrentTimeline(encoder_recovery_path);
  qunsetenv("PHOTO_EDITOR_TIMELINE_FORCE_HARDWARE_ENCODER_FAILURE");
  if (!encoder_recovered || !QFileInfo::exists(encoder_recovery_path) ||
      !deterministic_reopened.status().contains(
          QStringLiteral("completed export with the software encoder")) ||
      deterministic_reopened.deterministicDocumentHash() != encoder_recovery_hash) {
    return fail("hardware encoder device loss should retry with software, publish recovery, and preserve project state");
  }

  const auto large_preview_path = workspace.filePath(QStringLiteral("large-preview.png"));
  QImage large_preview(8, 8, QImage::Format_RGB32);
  large_preview.fill(Qt::darkGray);
  if (!large_preview.save(large_preview_path)) {
    return fail("incremental-load proof should create an immediately playable first image");
  }
  constexpr auto kStressTrackCount = 100;
  constexpr auto kStressMediaTrackCount = 98;
  constexpr auto kStressRuntimeTrackCount = kStressMediaTrackCount * 2 + 2;
  constexpr auto kStressClipsPerTrack = 100;
  constexpr auto kStressClipCount = kStressTrackCount * kStressClipsPerTrack;
  constexpr auto kStressDurationSeconds = 4.0 * 60.0 * 60.0;
  constexpr auto kStressMarkerCount = 2400;
  constexpr auto kStressCaptionCount = 200;
  QJsonArray large_tracks;
  for (auto track_index = 0; track_index < kStressTrackCount; ++track_index) {
    QJsonArray clips;
    for (auto clip_index = 0; clip_index < kStressClipsPerTrack; ++clip_index) {
      const auto global_index = track_index * kStressClipsPerTrack + clip_index;
      const auto first_clip = global_index == 0;
      const auto caption_clip = track_index >= kStressMediaTrackCount;
      const auto effected_clip = !caption_clip && global_index % 4 == 0;
      const auto start_seconds =
          static_cast<double>(clip_index) * (kStressDurationSeconds - 1.0) /
          static_cast<double>(kStressClipsPerTrack - 1);
      QJsonObject clip_object{
          {QStringLiteral("clipId"), QStringLiteral("large-%1").arg(global_index)},
          {QStringLiteral("assetId"),
           caption_clip ? QStringLiteral("generated:caption:%1").arg(global_index)
                        : QStringLiteral("fixture:%1").arg(global_index)},
          {QStringLiteral("mediaType"),
           caption_clip ? QStringLiteral("caption")
                        : first_clip ? QStringLiteral("photo") : QStringLiteral("video")},
          {QStringLiteral("filePath"), caption_clip
                                           ? QString{}
                                           : first_clip
                                           ? large_preview_path
                                           : workspace.filePath(
                                                 QStringLiteral("offline-%1.mp4").arg(global_index))},
          {QStringLiteral("displayLabel"),
           caption_clip ? QStringLiteral("Caption %1").arg(global_index)
                        : QStringLiteral("Clip %1").arg(global_index)},
          {QStringLiteral("linkedGroupId"), QStringLiteral("stress-link-%1").arg(global_index)},
          {QStringLiteral("durationSeconds"), 1.0},
          {QStringLiteral("trimStartSeconds"), 0.0},
          {QStringLiteral("trimEndSeconds"), 1.0},
          {QStringLiteral("startSeconds"), start_seconds},
          {QStringLiteral("hasAudio"), false},
      };
      if (caption_clip) {
        clip_object.insert(
            QStringLiteral("titleText"),
            QStringLiteral("Speaker %1 dense caption proof").arg((global_index % 4) + 1));
        clip_object.insert(
            QStringLiteral("captionWords"),
            QJsonArray{
                QJsonObject{{QStringLiteral("text"), QStringLiteral("dense")},
                            {QStringLiteral("startSeconds"), 0.0},
                            {QStringLiteral("endSeconds"), 0.45},
                            {QStringLiteral("speaker"), QStringLiteral("Speaker %1").arg((global_index % 4) + 1)},
                            {QStringLiteral("confidence"), 0.94}},
                QJsonObject{{QStringLiteral("text"), QStringLiteral("caption")},
                            {QStringLiteral("startSeconds"), 0.45},
                            {QStringLiteral("endSeconds"), 0.95},
                            {QStringLiteral("speaker"), QStringLiteral("Speaker %1").arg((global_index % 4) + 1)},
                            {QStringLiteral("confidence"), 0.92}},
            });
      } else if (effected_clip) {
        clip_object.insert(
            QStringLiteral("effectStack"),
            QJsonArray{QStringLiteral("Exposure"), QStringLiteral("Blur")});
        clip_object.insert(
            QStringLiteral("effectSettings"),
            QJsonObject{
                {QStringLiteral("Exposure"),
                 QJsonObject{{QStringLiteral("enabled"), true},
                             {QStringLiteral("amount"), 0.18}}},
                {QStringLiteral("Blur"),
                 QJsonObject{{QStringLiteral("enabled"), true},
                             {QStringLiteral("amount"), 0.12}}},
            });
      }
      clips.push_back(clip_object);
    }
    large_tracks.push_back(QJsonObject{
        {QStringLiteral("trackId"), QStringLiteral("video-%1").arg(track_index + 1)},
        {QStringLiteral("displayLabel"),
         track_index >= kStressMediaTrackCount
             ? QStringLiteral("CC%1").arg(track_index - kStressMediaTrackCount + 1)
             : QStringLiteral("V%1").arg(track_index + 1)},
        {QStringLiteral("kind"), QStringLiteral("video")},
        {QStringLiteral("role"),
         track_index >= kStressMediaTrackCount ? QStringLiteral("caption")
                                               : QStringLiteral("media")},
        {QStringLiteral("laneIndex"), track_index},
        {QStringLiteral("visible"), true},
        {QStringLiteral("clips"), clips},
    });
  }
  QJsonArray large_markers;
  for (auto marker_index = 0; marker_index < kStressMarkerCount; ++marker_index) {
    large_markers.push_back(QJsonObject{
        {QStringLiteral("markerId"), QStringLiteral("stress-marker-%1").arg(marker_index)},
        {QStringLiteral("label"), QStringLiteral("Review %1").arg(marker_index)},
        {QStringLiteral("note"), QStringLiteral("Dense marker note %1").arg(marker_index)},
        {QStringLiteral("seconds"), static_cast<double>(marker_index) * 6.0},
        {QStringLiteral("color"), QStringLiteral("#d7a84d")},
    });
  }
  const auto large_document_path = workspace.filePath(QStringLiteral("large-timeline.json"));
  QFile large_document_file(large_document_path);
  const QJsonObject large_document{
      {QStringLiteral("timelineId"), QStringLiteral("large-project-fixture")},
      {QStringLiteral("name"), QStringLiteral("10K Incremental Fixture")},
      {QStringLiteral("createdAt"), QStringLiteral("2026-07-19T00:00:00Z")},
      {QStringLiteral("version"), 8},
      {QStringLiteral("sequenceFormat"),
       QJsonObject{{QStringLiteral("width"), 1920},
                   {QStringLiteral("height"), 1080},
                   {QStringLiteral("frameRate"), 30.0},
                   {QStringLiteral("audioSampleRate"), 48000}}},
      {QStringLiteral("tracks"), large_tracks},
      {QStringLiteral("markers"), large_markers},
  };
  if (!large_document_file.open(QIODevice::WriteOnly) ||
      large_document_file.write(QJsonDocument(large_document).toJson(QJsonDocument::Compact)) <= 0) {
    return fail("incremental-load proof should write the 10,000-clip fixture");
  }
  large_document_file.close();

  const auto rss_before_stress_load = current_resident_bytes();
  TimelineDocumentController incremental;
  QElapsedTimer incremental_timer;
  incremental_timer.start();
  if (!incremental.loadTimeline(large_document_path)) {
    return fail("the 10,000-clip fixture should load its editorial graph");
  }
  const auto initial_load_ms = incremental_timer.elapsed();
  if (initial_load_ms > 2000 || incremental.clipCount() != kStressClipCount ||
      incremental.trackCount() != kStressRuntimeTrackCount ||
      incremental.videoTrackCount() != kStressMediaTrackCount ||
      incremental.captionCount() != kStressCaptionCount ||
      std::abs(incremental.totalDurationSecondsValue() - kStressDurationSeconds) > 0.001 ||
      incremental.currentIndex() < 0 ||
      incremental.previewSource().isEmpty() || !incremental.incrementalLoadBusy() ||
      incremental.incrementalLoadPendingCount() !=
          kStressClipCount - kStressCaptionCount ||
      incremental.incrementalLoadCompletedCount() != 0) {
    std::cerr << "stress load diagnostics: load_ms=" << initial_load_ms
              << " clips=" << incremental.clipCount()
              << " tracks=" << incremental.trackCount()
              << " video_tracks=" << incremental.videoTrackCount()
              << " captions=" << incremental.captionCount()
              << " duration=" << incremental.totalDurationSecondsValue()
              << " current_index=" << incremental.currentIndex()
              << " preview_empty=" << incremental.previewSource().isEmpty()
              << " hydration_busy=" << incremental.incrementalLoadBusy()
              << " hydration_pending=" << incremental.incrementalLoadPendingCount()
              << " hydration_complete=" << incremental.incrementalLoadCompletedCount()
              << '\n';
    return fail("large-project load should publish a four-hour, 100-track, 10,000-clip edit with dense captions and the first playable frame before deferred hydration executes");
  }
  QEventLoop incremental_event_loop;
  QTimer::singleShot(90, &incremental_event_loop, &QEventLoop::quit);
  incremental_event_loop.exec();
  if (incremental.incrementalLoadCompletedCount() <= 0 ||
      incremental.incrementalLoadPendingCount() <= 0 ||
      !incremental.incrementalLoadStatus().contains(QStringLiteral("Preparing media details")) ||
      incremental.clipCount() != kStressClipCount) {
    return fail("media hydration should advance in event-loop batches while the full edit remains interactive");
  }

  QElapsedTimer projection_timer;
  projection_timer.start();
  auto stress_track_model = incremental.sequenceTracks();
  const auto projection_ms = projection_timer.elapsed();
  const auto stress_marker_model = incremental.sequenceMarkers();
  const auto stress_transcript_model = incremental.transcriptSegments();
  auto projected_clip_count = 0;
  auto projected_effect_count = 0;
  auto projected_offline_count = 0;
  for (const auto& track_value : stress_track_model) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackKind")).toString() != QStringLiteral("video")) {
      continue;
    }
    const auto clips = track.value(QStringLiteral("clips")).toList();
    projected_clip_count += static_cast<int>(clips.size());
    for (const auto& clip_value : clips) {
      const auto clip = clip_value.toMap();
      if (!clip.value(QStringLiteral("effectStack")).toList().isEmpty()) {
        ++projected_effect_count;
      }
      if (!clip.value(QStringLiteral("mediaOnline")).toBool()) {
        ++projected_offline_count;
      }
    }
  }
  const auto rss_with_full_projection = current_resident_bytes();
  constexpr qint64 kMebibyte = 1024LL * 1024LL;
  if (projection_ms > 1500 || stress_track_model.size() != kStressRuntimeTrackCount ||
      projected_clip_count != kStressClipCount ||
      stress_marker_model.size() != kStressMarkerCount ||
      stress_transcript_model.size() != kStressCaptionCount ||
      projected_effect_count < 2000 || projected_offline_count < 9000 ||
      (rss_before_stress_load >= 0 && rss_with_full_projection >= 0 &&
       rss_with_full_projection - rss_before_stress_load > 512LL * kMebibyte)) {
    std::cerr << "stress projection diagnostics: projection_ms=" << projection_ms
              << " track_rows=" << stress_track_model.size()
              << " clips=" << projected_clip_count
              << " markers=" << stress_marker_model.size()
              << " transcripts=" << stress_transcript_model.size()
              << " effected=" << projected_effect_count
              << " offline=" << projected_offline_count
              << " rss_growth_mib="
              << (rss_before_stress_load >= 0 && rss_with_full_projection >= 0
                      ? (rss_with_full_projection - rss_before_stress_load) / kMebibyte
                      : -1)
              << '\n';
    return fail("the full visible stress model should project 10,000 mixed clips, dense effects, captions, markers, and offline media within 1.5 seconds and 512 MiB");
  }

  stress_track_model.clear();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  // Warm allocation reuse across complete projections before measuring retained
  // growth. A single cold projection does not establish steady allocation reuse.
  for (int warmup = 0; warmup < 4; ++warmup) {
    const auto warm_model = incremental.sequenceTracks();
    if (warm_model.size() != kStressRuntimeTrackCount)
      return fail("large-project projection warmup should remain complete");
  }
  const auto rss_after_warm_projection = current_resident_bytes();
  for (auto repeat = 0; repeat < 4; ++repeat) {
    const auto repeated_model = incremental.sequenceTracks();
    if (repeated_model.size() != kStressRuntimeTrackCount) {
      return fail("repeated large-project model projection should remain complete");
    }
  }
  const auto rss_after_repeated_projection = current_resident_bytes();
  if (rss_after_warm_projection >= 0 && rss_after_repeated_projection >= 0 &&
      rss_after_repeated_projection - rss_after_warm_projection > 128LL * kMebibyte) {
    return fail("repeated 10,000-clip model projection should not retain more than 128 MiB after warmup");
  }

  QVector<qint64> seek_samples_ms;
  QVector<qint64> selection_samples_ms;
  seek_samples_ms.reserve(32);
  selection_samples_ms.reserve(32);
  for (auto sample = 0; sample < 32; ++sample) {
    const auto clip_slot = (sample * 37) % kStressClipsPerTrack;
    const auto target_seconds =
        static_cast<double>(clip_slot) * (kStressDurationSeconds - 1.0) /
        static_cast<double>(kStressClipsPerTrack - 1);
    QElapsedTimer sample_timer;
    sample_timer.start();
    incremental.seekSequenceSeconds(target_seconds);
    seek_samples_ms.push_back(sample_timer.elapsed());

    sample_timer.restart();
    incremental.selectClip((sample * 313) % kStressClipCount);
    selection_samples_ms.push_back(sample_timer.elapsed());
  }
  const auto seek_p95_ms = percentile_95(seek_samples_ms);
  const auto selection_p95_ms = percentile_95(selection_samples_ms);
  if (seek_p95_ms > 100 || selection_p95_ms > 100 ||
      !incremental.incrementalLoadBusy()) {
    return fail("seek and selection should stay below 100 ms p95 while large-project hydration remains active");
  }

  incremental.selectClip(0);
  QVector<qint64> edit_samples_ms;
  edit_samples_ms.reserve(12);
  for (auto edit_index = 0; edit_index < 12; ++edit_index) {
    QElapsedTimer edit_timer;
    edit_timer.start();
    incremental.setSelectedClipEffectAmount(
        QStringLiteral("Exposure"), -0.6 + static_cast<double>(edit_index) * 0.1);
    edit_samples_ms.push_back(edit_timer.elapsed());
  }
  const auto edit_p95_ms = percentile_95(edit_samples_ms);
  auto retained_undo_depth = 0;
  QVector<qint64> undo_samples_ms;
  while (incremental.canUndo()) {
    QElapsedTimer undo_timer;
    undo_timer.start();
    incremental.undo();
    undo_samples_ms.push_back(undo_timer.elapsed());
    ++retained_undo_depth;
  }
  const auto undo_p95_ms = percentile_95(undo_samples_ms);
  const auto rss_after_bounded_edits = current_resident_bytes();
  if (edit_p95_ms > 250 || undo_p95_ms > 250 || retained_undo_depth != 8 ||
      (rss_before_stress_load >= 0 && rss_after_bounded_edits >= 0 &&
       rss_after_bounded_edits - rss_before_stress_load > 640LL * kMebibyte)) {
    return fail("large-project edits and undo should stay below 250 ms p95 with an eight-snapshot bounded history and a 640 MiB process-growth ceiling");
  }

  std::cout << "timeline scale proof: load=" << initial_load_ms
            << "ms projection=" << projection_ms
            << "ms seek_p95=" << seek_p95_ms
            << "ms selection_p95=" << selection_p95_ms
            << "ms edit_p95=" << edit_p95_ms
            << "ms undo_p95=" << undo_p95_ms
            << "ms rss_growth_mib="
            << (rss_before_stress_load >= 0 && rss_after_bounded_edits >= 0
                    ? (rss_after_bounded_edits - rss_before_stress_load) / kMebibyte
                    : -1)
            << '\n';

  return EXIT_SUCCESS;
}
