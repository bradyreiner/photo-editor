// Staged Round 13 public-API regression fixture. Root owns target integration.
// Holds waveform A in a private ffmpeg wrapper, queues B and C, removes B before
// it starts, then verifies C drains and a same-path restore can retry B.

#include <cstdlib>
#include <functional>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "timeline_document_controller.h"

namespace {

using photo_editor::desktop::TimelineDocumentController;

constexpr int kWaitTimeoutMs = 30000;

int fail(const QString& message) {
  std::cerr << "timeline_waveform_queue_disappearance: "
            << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

void emit_row(const QJsonObject& row) {
  std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << std::endl;
}

bool wait_for(const std::function<bool()>& predicate, int timeout_ms = kWaitTimeoutMs) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (predicate()) return true;
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

QVariantMap derived_work(const TimelineDocumentController& document) {
  return document.agentExportSnapshotReadiness()
      .value(QStringLiteral("derivedWork"))
      .toMap();
}

int waveform_queue_count(const TimelineDocumentController& document) {
  return derived_work(document).value(QStringLiteral("waveformQueueCount")).toInt();
}

bool waveform_busy(const TimelineDocumentController& document) {
  return derived_work(document).value(QStringLiteral("waveformActive")).toBool();
}

bool all_derived_work_idle(const TimelineDocumentController& document) {
  const auto work = derived_work(document);
  return !document.mediaAnalysisBusy() && !document.incrementalLoadBusy() &&
      !work.value(QStringLiteral("sourceIntegrityActive")).toBool() &&
      work.value(QStringLiteral("sourceIntegrityQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("waveformActive")).toBool() &&
      work.value(QStringLiteral("waveformQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("posterActive")).toBool() &&
      work.value(QStringLiteral("posterQueueCount")).toInt() == 0;
}

bool touch_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

bool create_audio_fixture(const QString& ffmpeg, const QString& output_path, int frequency) {
  QProcess process;
  process.start(
      ffmpeg,
      {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=2").arg(frequency),
       QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), output_path});
  return process.waitForFinished(30000) &&
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
      QFileInfo(output_path).isReadable() && QFileInfo(output_path).size() > 0;
}

bool create_blocking_ffmpeg_wrapper(const QString& path) {
  QFile wrapper(path);
  if (!wrapper.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
  const QByteArray script =
      "#!/bin/sh\n"
      "input=\"\"\n"
      "want_input=0\n"
      "for arg in \"$@\"; do\n"
      "  if [ \"$want_input\" -eq 1 ]; then input=\"$arg\"; want_input=0; continue; fi\n"
      "  if [ \"$arg\" = \"-i\" ]; then want_input=1; fi\n"
      "done\n"
      "printf '%s\\n' \"$input\" >> \"$PHOTO_EDITOR_QUEUE_FFMPEG_LOG\"\n"
      "if [ \"$input\" = \"$PHOTO_EDITOR_QUEUE_BLOCKED_SOURCE\" ]; then\n"
      "  : > \"$PHOTO_EDITOR_QUEUE_STARTED_MARKER\"\n"
      "  while [ ! -e \"$PHOTO_EDITOR_QUEUE_RELEASE_MARKER\" ]; do sleep 1; done\n"
      "fi\n"
      "exec \"$PHOTO_EDITOR_QUEUE_REAL_FFMPEG\" \"$@\"\n";
  if (wrapper.write(script) != script.size()) return false;
  wrapper.close();
  return wrapper.setPermissions(
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
      QFileDevice::ExeOther);
}

QStringList invocation_inputs(const QString& log_path) {
  QFile log(log_path);
  if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QStringList inputs;
  while (!log.atEnd()) inputs.push_back(QString::fromUtf8(log.readLine()).trimmed());
  return inputs;
}

int invocation_count(const QStringList& inputs, const QString& source_path) {
  auto count = 0;
  for (const auto& input : inputs) {
    if (input == source_path) ++count;
  }
  return count;
}

QString waveform_source_for_path(
    const TimelineDocumentController& document,
    const QString& source_path) {
  for (const auto& value : document.clips()) {
    const auto clip = value.toMap();
    if (clip.value(QStringLiteral("filePath")).toString() == source_path) {
      return clip.value(QStringLiteral("waveformSource")).toString();
    }
  }
  return {};
}

bool has_clip_source_path(
    const TimelineDocumentController& document,
    const QString& source_path) {
  for (const auto& value : document.clips()) {
    if (value.toMap().value(QStringLiteral("filePath")).toString() == source_path) {
      return true;
    }
  }
  return false;
}

QSize read_waveform_size(const QString& waveform_source) {
  if (waveform_source.isEmpty()) return {};
  const auto url = QUrl(waveform_source);
  const auto path = url.isLocalFile() ? url.toLocalFile() : waveform_source;
  QImageReader reader(path);
  reader.setAutoTransform(true);
  return reader.read().size();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) return fail(QStringLiteral("temporary workspace is unavailable"));
  const auto app_data_root = workspace.filePath(QStringLiteral("app-data"));
  const auto cache_root = workspace.filePath(QStringLiteral("cache"));
  const auto settings_root = workspace.filePath(QStringLiteral("settings"));
  if (!QDir().mkpath(app_data_root) || !QDir().mkpath(cache_root) ||
      !QDir().mkpath(settings_root)) {
    return fail(QStringLiteral("private runtime roots could not be created"));
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/looksy"));
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyRound13Lifecycle"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("round13.looksy.test"));
  QCoreApplication::setApplicationName(QStringLiteral("WaveformQueueDisappearance"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings_root);

  const auto real_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (real_ffmpeg.isEmpty()) {
    std::cerr << "SKIP: ffmpeg is required for the real waveform sources\n";
    return 77;
  }
  const auto source_a = workspace.filePath(QStringLiteral("clip-a.wav"));
  const auto source_b = workspace.filePath(QStringLiteral("clip-b.wav"));
  const auto source_c = workspace.filePath(QStringLiteral("clip-c.wav"));
  const auto parked_b = workspace.filePath(QStringLiteral("clip-b.offline.wav"));
  if (!create_audio_fixture(real_ffmpeg, source_a, 440) ||
      !create_audio_fixture(real_ffmpeg, source_b, 554) ||
      !create_audio_fixture(real_ffmpeg, source_c, 659)) {
    return fail(QStringLiteral("the deterministic PCM waveform sources could not be created"));
  }

  const auto fake_bin = workspace.filePath(QStringLiteral("fake-bin"));
  if (!QDir().mkpath(fake_bin)) return fail(QStringLiteral("private fake-bin is unavailable"));
  const auto wrapper_path = QDir(fake_bin).filePath(QStringLiteral("ffmpeg"));
  const auto invocation_log = workspace.filePath(QStringLiteral("ffmpeg-inputs.log"));
  const auto a_started = workspace.filePath(QStringLiteral("clip-a-started"));
  const auto release_a = workspace.filePath(QStringLiteral("release-clip-a"));
  if (!create_blocking_ffmpeg_wrapper(wrapper_path)) {
    return fail(QStringLiteral("the private blocking ffmpeg wrapper could not be created"));
  }
  const auto old_path = qgetenv("PATH");
  const auto restore_path = qScopeGuard([old_path]() { qputenv("PATH", old_path); });
  const auto release_guard = qScopeGuard([release_a]() {
    if (!QFileInfo::exists(release_a)) static_cast<void>(touch_file(release_a));
  });
  qputenv("PHOTO_EDITOR_QUEUE_FFMPEG_LOG", invocation_log.toUtf8());
  qputenv("PHOTO_EDITOR_QUEUE_BLOCKED_SOURCE", source_a.toUtf8());
  qputenv("PHOTO_EDITOR_QUEUE_STARTED_MARKER", a_started.toUtf8());
  qputenv("PHOTO_EDITOR_QUEUE_RELEASE_MARKER", release_a.toUtf8());
  qputenv("PHOTO_EDITOR_QUEUE_REAL_FFMPEG", real_ffmpeg.toUtf8());
  qputenv("PATH", fake_bin.toUtf8() + QByteArrayLiteral(":") + old_path);
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")) != wrapper_path) {
    return fail(QStringLiteral("the private ffmpeg wrapper did not take PATH precedence"));
  }

  QString saved_document_path;
  int b_drain_state_notifications = 0;
  int final_idle_state_notifications = 0;
  int queued_before_b_removal = -1;
  int queued_after_b_removal = -1;
  int a_invocations_before_retry = 0;
  int b_invocations_before_retry = 0;
  int c_invocations_before_retry = 0;
  QString offline_b_waveform_source;
  {
    TimelineDocumentController document;
    const auto release_a_before_controller_teardown = qScopeGuard([release_a]() {
      if (!QFileInfo::exists(release_a)) static_cast<void>(touch_file(release_a));
    });
    document.createTimeline(QStringLiteral("Waveform queued source disappears"));
    document.addClipFromGallery(
        QStringLiteral("audio:queue-a"), source_a, QStringLiteral("Queued A"), 2.0, {});
    if (!wait_for([&a_started]() { return QFileInfo(a_started).isReadable(); })) {
      return fail(QStringLiteral("waveform A did not enter the controlled blocked wrapper"));
    }

    document.addClipFromGallery(
        QStringLiteral("audio:queue-b"), source_b, QStringLiteral("Queued B"), 2.0, {});
    document.addClipFromGallery(
        QStringLiteral("audio:queue-c"), source_c, QStringLiteral("Queued C"), 2.0, {});
    document.selectClip(1);
    if (document.clipCount() != 3 || !document.saveTimeline()) {
      return fail(QStringLiteral("three audio clips could not be saved while A was held"));
    }
    saved_document_path = document.savedDocumentPath();
    if (saved_document_path.isEmpty() || !QFileInfo(saved_document_path).isReadable()) {
      return fail(QStringLiteral("the same-path restore Timeline was not persisted"));
    }
    if (!wait_for([&]() {
          return waveform_busy(document) && waveform_queue_count(document) == 2;
        })) {
      return fail(QStringLiteral("B and C did not remain queued behind blocked A"));
    }
    queued_before_b_removal = waveform_queue_count(document);
    if (!QFile::rename(source_b, parked_b)) {
      return fail(QStringLiteral("queued B source could not be moved offline"));
    }

    QObject::connect(
        &document, &TimelineDocumentController::timelineStateChanged, &document,
        [&]() {
          const auto queued = waveform_queue_count(document);
          const auto inputs = invocation_inputs(invocation_log);
          if (!QFileInfo(source_b).isReadable() && queued == 1 &&
              invocation_count(inputs, source_c) == 0) {
            ++b_drain_state_notifications;
            queued_after_b_removal = queued;
          }
          if (!waveform_busy(document) && queued == 0) {
            ++final_idle_state_notifications;
          }
        },
        Qt::DirectConnection);

    if (!touch_file(release_a)) {
      return fail(QStringLiteral("the blocked A wrapper could not be released"));
    }
    if (!wait_for([&]() {
          const auto inputs = invocation_inputs(invocation_log);
          return !waveform_busy(document) && waveform_queue_count(document) == 0 &&
              invocation_count(inputs, source_a) == 1 &&
              invocation_count(inputs, source_c) == 1;
        })) {
      return fail(QStringLiteral("waveform queue did not drain from A through online C"));
    }
    const auto inputs = invocation_inputs(invocation_log);
    a_invocations_before_retry = invocation_count(inputs, source_a);
    b_invocations_before_retry = invocation_count(inputs, source_b);
    c_invocations_before_retry = invocation_count(inputs, source_c);
    offline_b_waveform_source = waveform_source_for_path(document, source_b);
    const auto c_waveform_source = waveform_source_for_path(document, source_c);
    const auto c_waveform_size = read_waveform_size(c_waveform_source);
    if (a_invocations_before_retry != 1 || b_invocations_before_retry != 0 ||
        c_invocations_before_retry != 1 || b_drain_state_notifications == 0 ||
        queued_before_b_removal != 2 || queued_after_b_removal != 1 ||
        final_idle_state_notifications == 0 || document.clipCount() != 3 ||
        QFileInfo(source_b).isReadable() || !offline_b_waveform_source.isEmpty() ||
        !has_clip_source_path(document, source_b) || c_waveform_size != QSize(2400, 160)) {
      emit_row({
          {QStringLiteral("phase"), QStringLiteral("queued-offline-drain-failed")},
          {QStringLiteral("aInvocations"), a_invocations_before_retry},
          {QStringLiteral("bInvocations"), b_invocations_before_retry},
          {QStringLiteral("cInvocations"), c_invocations_before_retry},
          {QStringLiteral("bDrainStateNotifications"), b_drain_state_notifications},
          {QStringLiteral("finalIdleStateNotifications"), final_idle_state_notifications},
          {QStringLiteral("queuedBeforeBRemoval"), queued_before_b_removal},
          {QStringLiteral("queuedAfterBRemoval"), queued_after_b_removal},
          {QStringLiteral("waveformQueueCount"), waveform_queue_count(document)},
          {QStringLiteral("waveformBusy"), waveform_busy(document)},
          {QStringLiteral("cWaveformSource"), c_waveform_source},
          {QStringLiteral("cWaveformWidth"), c_waveform_size.width()},
          {QStringLiteral("cWaveformHeight"), c_waveform_size.height()},
      });
      return fail(QStringLiteral("queue removal must notify observers, skip offline B, and complete C"));
    }
    emit_row({
        {QStringLiteral("phase"), QStringLiteral("queued-offline-drained")},
        {QStringLiteral("aInvocations"), a_invocations_before_retry},
        {QStringLiteral("bInvocations"), b_invocations_before_retry},
        {QStringLiteral("cInvocations"), c_invocations_before_retry},
        {QStringLiteral("bDrainStateNotifications"), b_drain_state_notifications},
        {QStringLiteral("finalIdleStateNotifications"), final_idle_state_notifications},
        {QStringLiteral("queuedBeforeBRemoval"), queued_before_b_removal},
        {QStringLiteral("queuedAfterBRemoval"), queued_after_b_removal},
        {QStringLiteral("waveformQueueCount"), waveform_queue_count(document)},
        {QStringLiteral("waveformBusy"), waveform_busy(document)},
        {QStringLiteral("cWaveformSource"), c_waveform_source},
        {QStringLiteral("cWaveformWidth"), c_waveform_size.width()},
        {QStringLiteral("cWaveformHeight"), c_waveform_size.height()},
        {QStringLiteral("savedDocumentPath"), saved_document_path},
    });
  }

  if (!QFile::rename(parked_b, source_b)) {
    return fail(QStringLiteral("B could not be restored at its exact original path"));
  }
  TimelineDocumentController retry;
  if (!retry.loadTimeline(saved_document_path) ||
      !wait_for([&retry]() { return all_derived_work_idle(retry); })) {
    return fail(QStringLiteral("same-path B restoration did not settle on a fresh Timeline load"));
  }
  const auto retry_inputs = invocation_inputs(invocation_log);
  const auto retry_b_invocations = invocation_count(retry_inputs, source_b);
  const auto restored_waveform_source = waveform_source_for_path(retry, source_b);
  const auto restored_size = read_waveform_size(restored_waveform_source);
  if (retry.clipCount() != 3 || retry_b_invocations != 1 ||
      restored_waveform_source.isEmpty() || restored_size != QSize(2400, 160) ||
      !has_clip_source_path(retry, source_b)) {
    return fail(QStringLiteral("restored B must retry once and publish its real 2400x160 waveform"));
  }
  emit_row({
      {QStringLiteral("phase"), QStringLiteral("same-path-b-restored")},
      {QStringLiteral("bInvocationsBeforeRestore"), b_invocations_before_retry},
      {QStringLiteral("bInvocationsAfterRestore"), retry_b_invocations},
      {QStringLiteral("samePath"), source_b},
      {QStringLiteral("waveformSource"), restored_waveform_source},
      {QStringLiteral("waveformWidth"), restored_size.width()},
      {QStringLiteral("waveformHeight"), restored_size.height()},
      {QStringLiteral("waveformQueueCount"), waveform_queue_count(retry)},
      {QStringLiteral("waveformBusy"), waveform_busy(retry)},
  });
  return EXIT_SUCCESS;
}
