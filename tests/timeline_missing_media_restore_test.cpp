// Staged Round 13 fixture. The root agent owns CMake integration/builds.
// Exercises repeated restoration of one persisted audio-bearing Timeline while
// its source is missing, then retries after the original path becomes readable.

#include <cstdlib>
#include <ctime>
#include <iostream>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include "timeline_document_controller.h"

namespace {

using photo_editor::desktop::TimelineDocumentController;

constexpr int kMissingReopenCount = 3;
constexpr int kWaitTimeoutMs = 30000;

int fail(const QString& message) {
  std::cerr << "timeline_missing_media_restore: " << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QVariantMap derived_work(const TimelineDocumentController& controller) {
  return controller.agentExportSnapshotReadiness()
      .value(QStringLiteral("derivedWork"))
      .toMap();
}

bool derived_work_idle(const TimelineDocumentController& controller) {
  const auto work = derived_work(controller);
  return !controller.mediaAnalysisBusy() && !controller.incrementalLoadBusy() &&
      !work.value(QStringLiteral("sourceIntegrityActive")).toBool() &&
      work.value(QStringLiteral("sourceIntegrityQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("waveformActive")).toBool() &&
      work.value(QStringLiteral("waveformQueueCount")).toInt() == 0 &&
      !work.value(QStringLiteral("posterActive")).toBool() &&
      work.value(QStringLiteral("posterQueueCount")).toInt() == 0;
}

bool wait_for_derived_work(TimelineDocumentController& controller) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < kWaitTimeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (derived_work_idle(controller)) return true;
    QThread::msleep(2);
  }
  return derived_work_idle(controller);
}

QString core_clip_signature(const TimelineDocumentController& controller) {
  QJsonArray core_clips;
  for (const auto& clip_value : controller.clips()) {
    const auto clip = clip_value.toMap();
    QJsonObject core;
    for (const auto& key : {
             QStringLiteral("clipId"), QStringLiteral("filePath"),
             QStringLiteral("mediaType"), QStringLiteral("startSeconds"),
             QStringLiteral("durationSeconds"), QStringLiteral("trimStartSeconds"),
             QStringLiteral("trimEndSeconds"), QStringLiteral("hasAudio"),
             QStringLiteral("sourceFileSize"), QStringLiteral("sourceIdentitySignature")}) {
      if (clip.contains(key)) core.insert(key, QJsonValue::fromVariant(clip.value(key)));
    }
    core_clips.push_back(core);
  }
  return QString::fromUtf8(QJsonDocument(core_clips).toJson(QJsonDocument::Compact));
}

int wrapper_invocation_count(const QString& log_path) {
  QFile log(log_path);
  if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
  int count = 0;
  while (!log.atEnd()) {
    static_cast<void>(log.readLine());
    ++count;
  }
  return count;
}

void emit_row(const QJsonObject& row) {
  std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << std::endl;
}

bool create_audio_fixture(const QString& ffmpeg, const QString& output_path) {
  QProcess process;
  process.start(
      ffmpeg,
      {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("sine=frequency=440:sample_rate=48000:duration=3"),
       QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), output_path});
  return process.waitForFinished(30000) &&
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
      QFileInfo(output_path).isReadable() && QFileInfo(output_path).size() > 0;
}

bool create_failing_ffmpeg_wrapper(const QString& path) {
  QFile wrapper(path);
  if (!wrapper.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
  const QByteArray script =
      "#!/bin/sh\n"
      "printf 'invoked\\n' >> \"$PHOTO_EDITOR_MISSING_MEDIA_FFMPEG_LOG\"\n"
      "exit 1\n";
  if (wrapper.write(script) != script.size()) return false;
  wrapper.close();
  return wrapper.setPermissions(
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
      QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
      QFileDevice::ExeOther);
}

QImage read_waveform_image(const QVariantList& clips) {
  for (const auto& clip_value : clips) {
    const auto clip = clip_value.toMap();
    const auto value = clip.value(QStringLiteral("waveformSource")).toString();
    if (value.isEmpty()) continue;
    const QUrl url(value);
    const auto path = url.isLocalFile() ? url.toLocalFile() : value;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (!image.isNull()) return image;
  }
  return {};
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
    return fail(QStringLiteral("isolated application and cache roots could not be created"));
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/looksy"));
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyRound13Lifecycle"));
  QCoreApplication::setApplicationName(QStringLiteral("MissingMediaRestoreFixture"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("round13.looksy.test"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings_root);

  const auto real_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (real_ffmpeg.isEmpty()) {
    std::cerr << "SKIP: ffmpeg is required to create and revalidate the audio fixture\n";
    return 77;
  }
  const auto source_path = workspace.filePath(QStringLiteral("recoverable-source.wav"));
  const auto parked_source_path = workspace.filePath(QStringLiteral("recoverable-source.parked.wav"));
  if (!create_audio_fixture(real_ffmpeg, source_path)) {
    return fail(QStringLiteral("ffmpeg could not create the real WAV fixture"));
  }

  QString saved_document_path;
  {
    TimelineDocumentController author;
    author.createTimeline(QStringLiteral("Missing media restore retry"));
    author.addClipFromGallery(
        QStringLiteral("audio:missing-media-restore"), source_path,
        QStringLiteral("Recoverable audio"), 3.0, QString{});
    if (author.clipCount() != 1 || !author.saveTimeline() ||
        !wait_for_derived_work(author)) {
      return fail(QStringLiteral("the persisted real audio Timeline did not settle before source removal"));
    }
    saved_document_path = author.savedDocumentPath();
    if (saved_document_path.isEmpty() || !QFileInfo(saved_document_path).isReadable()) {
      return fail(QStringLiteral("the canonical Timeline file was not saved"));
    }
  }

  // Remove the first successful waveform so each missing-source restore starts
  // without a cache hit that could mask a needless ffmpeg retry.
  const auto waveform_cache_path = QDir(cache_root).filePath(QStringLiteral("timeline-waveforms"));
  if (QFileInfo::exists(waveform_cache_path) && !QDir(waveform_cache_path).removeRecursively()) {
    return fail(QStringLiteral("the isolated waveform cache could not be cleared for the fixture"));
  }
  if (!QFile::rename(source_path, parked_source_path)) {
    return fail(QStringLiteral("the real source could not be moved offline"));
  }

  const auto fake_bin = workspace.filePath(QStringLiteral("fake-bin"));
  if (!QDir().mkpath(fake_bin)) return fail(QStringLiteral("fake executable directory is unavailable"));
  const auto invocation_log = workspace.filePath(QStringLiteral("ffmpeg-invocations.log"));
  const auto wrapper_path = QDir(fake_bin).filePath(QStringLiteral("ffmpeg"));
  if (!create_failing_ffmpeg_wrapper(wrapper_path)) {
    return fail(QStringLiteral("the controlled ffmpeg wrapper could not be created"));
  }
  const auto old_path = qgetenv("PATH");
  const auto restore_path = qScopeGuard([&]() { qputenv("PATH", old_path); });
  qputenv("PHOTO_EDITOR_MISSING_MEDIA_FFMPEG_LOG", invocation_log.toUtf8());
  qputenv("PATH", (fake_bin.toUtf8() + QByteArrayLiteral(":") + old_path));
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")) != wrapper_path) {
    return fail(QStringLiteral("the test wrapper did not take precedence for missing-source probes"));
  }

  int missing_source_probe_count = 0;
  QString offline_core_signature;
  QString offline_document_hash;
  for (int cycle = 0; cycle < kMissingReopenCount; ++cycle) {
    TimelineDocumentController recovered;
    const auto cpu_start = std::clock();
    QElapsedTimer load_timer;
    load_timer.start();
    if (!recovered.loadTimeline(saved_document_path)) {
      return fail(QStringLiteral("Timeline reload failed while its source was missing"));
    }
    const auto load_ms = load_timer.elapsed();
    const auto load_cpu_ms = 1000.0 *
        static_cast<double>(std::clock() - cpu_start) / static_cast<double>(CLOCKS_PER_SEC);
    QElapsedTimer settle_timer;
    settle_timer.start();
    if (!wait_for_derived_work(recovered)) {
      return fail(QStringLiteral("missing-source hydration did not settle"));
    }
    const auto settle_ms = settle_timer.elapsed();
    const auto clips = recovered.clips();
    if (recovered.clipCount() != 1 || clips.size() != 1 ||
        !recovered.hasOfflineMediaItems() || recovered.offlineMediaItems().size() != 1 ||
        clips.first().toMap().value(QStringLiteral("filePath")).toString() != source_path ||
        !derived_work_idle(recovered)) {
      return fail(QStringLiteral("offline restoration changed or dropped the authored media clip"));
    }
    const auto core_signature = core_clip_signature(recovered);
    const auto document_hash = recovered.deterministicDocumentHash();
    if (cycle == 0) {
      offline_core_signature = core_signature;
      offline_document_hash = document_hash;
    } else if (core_signature != offline_core_signature || document_hash != offline_document_hash) {
      return fail(QStringLiteral("repeated offline restoration changed the saved edit state"));
    }
    missing_source_probe_count = wrapper_invocation_count(invocation_log);
    emit_row({
        {QStringLiteral("phase"), QStringLiteral("offline-reopen")},
        {QStringLiteral("cycle"), cycle},
        {QStringLiteral("loadMs"), load_ms},
        {QStringLiteral("loadProcessCpuMs"), load_cpu_ms},
        {QStringLiteral("settleMs"), settle_ms},
        {QStringLiteral("clipCount"), recovered.clipCount()},
        {QStringLiteral("offlineCount"), recovered.offlineMediaItems().size()},
        {QStringLiteral("wrapperInvocationsTotal"), missing_source_probe_count},
        {QStringLiteral("documentHash"), document_hash},
        {QStringLiteral("coreClipSignature"), core_signature},
    });
  }

  qputenv("PATH", old_path);
  if (!QFile::rename(parked_source_path, source_path)) {
    return fail(QStringLiteral("the same source could not be restored at its original path"));
  }

  TimelineDocumentController retried;
  const auto retry_cpu_start = std::clock();
  QElapsedTimer retry_timer;
  retry_timer.start();
  if (!retried.loadTimeline(saved_document_path)) {
    return fail(QStringLiteral("Timeline reload failed after restoring the source"));
  }
  if (!wait_for_derived_work(retried)) {
    return fail(QStringLiteral("restored-source media analysis and waveform generation did not settle"));
  }
  const auto retry_ms = retry_timer.elapsed();
  const auto retry_cpu_ms = 1000.0 *
      static_cast<double>(std::clock() - retry_cpu_start) / static_cast<double>(CLOCKS_PER_SEC);
  const auto retried_clips = retried.clips();
  const auto waveform_image = read_waveform_image(retried_clips);
  const auto wrapper_count_after_restore = wrapper_invocation_count(invocation_log);
  emit_row({
      {QStringLiteral("phase"), QStringLiteral("same-path-restored-retry")},
      {QStringLiteral("loadAndSettleMs"), retry_ms},
      {QStringLiteral("loadAndSettleProcessCpuMs"), retry_cpu_ms},
      {QStringLiteral("clipCount"), retried.clipCount()},
      {QStringLiteral("offlineCount"), retried.offlineMediaItems().size()},
      {QStringLiteral("waveformImageValid"), !waveform_image.isNull()},
      {QStringLiteral("waveformWidth"), waveform_image.width()},
      {QStringLiteral("waveformHeight"), waveform_image.height()},
      {QStringLiteral("missingSourceWrapperInvocations"), wrapper_count_after_restore},
      {QStringLiteral("documentHash"), retried.deterministicDocumentHash()},
      {QStringLiteral("coreClipSignature"), core_clip_signature(retried)},
  });

  if (retried.clipCount() != 1 || retried.hasOfflineMediaItems() ||
      retried.offlineMediaItems().size() != 0 || waveform_image.isNull() ||
      core_clip_signature(retried) != offline_core_signature ||
      wrapper_count_after_restore != missing_source_probe_count) {
    return fail(QStringLiteral("same-path source restoration must preserve the clip and generate a readable waveform without using the missing-source wrapper"));
  }

  if (missing_source_probe_count != 0) {
    return fail(QStringLiteral("offline Timeline restores launched doomed ffmpeg waveform probes"));
  }
  std::cout << "timeline_missing_media_restore: offline reopen count=" << kMissingReopenCount
            << "; doomed ffmpeg launches=0; same-path restore waveform=readable; authored clip preserved\n";
  return EXIT_SUCCESS;
}
