#include "timeline_document_controller.h"
#include "timeline_asset_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {
struct TimelineHistoryTestAccess {
  static void protect(TimelineDocumentController& c, const QString& path) {
    c.tracks_[0].clips[1997].file_path = path;
    c.tracks_[0].clips[1997].poster_source = path;
    c.invalidateProjectCacheSummary();
  }
  static void poster(TimelineDocumentController& c, const QString& path) {
    c.tracks_[0].clips[1996].poster_source = path;
    c.invalidateProjectCacheSummary();
  }
  static void pressureArtifact(TimelineDocumentController& c, const QString& path) {
    c.tracks_[0].clips[0].poster_source = path;
    c.cache_budgets_.disk_bytes = 1024LL * 1024 * 1024;
    c.invalidateProjectCacheSummary();
  }
  static void start(TimelineDocumentController& c) { c.startProjectCacheInspection(); }
  static auto future(TimelineDocumentController& c) { return c.cache_inspection_watcher_.future(); }
  static void rename(TimelineDocumentController& c, const QString& id) { c.timeline_id_ = id; c.invalidateProjectCacheSummary(); }
  static void seed(TimelineDocumentController& c, const QString& root, int files) {
    c.tracks_.clear();
    c.has_active_timeline_ = true;
    c.timeline_id_ = "cache-fixture";
    c.timeline_name_ = "Cache fixture";
    c.cache_pressure_override_for_testing_ = "normal";
    c.cache_pressure_timer_.stop();
    c.cache_budgets_.automatic = false;
    c.cache_budgets_.disk_bytes = 1024LL * 1024 * 1024;
    TimelineDocumentController::Track track;
    track.track_id = "video-0";
    for (int i = 0; i < files; ++i) {
      TimelineDocumentController::Clip clip;
      clip.clip_id = QString::number(i);
      clip.media_type = "photo";
      clip.poster_source = QDir(root).filePath(QStringLiteral("cache/timeline-posters/%1.png").arg(i));
      track.clips.append(clip);
    }
    c.tracks_.append(track);
  }
};
}
using namespace photo_editor::desktop;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
  QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void refresh(TimelineDocumentController& c) {
  if (c.metaObject()->indexOfMethod("refreshProjectCacheSummary()") >= 0)
    QMetaObject::invokeMethod(&c, "refreshProjectCacheSummary", Qt::DirectConnection);
}
QVariantMap await_report(TimelineDocumentController& c) {
  QElapsedTimer t; t.start();
  auto report = c.projectCacheSummary();
  while (report.value("loading").toBool() && t.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
    report = c.projectCacheSummary();
  }
  require(!report.value("loading").toBool(), "Cache report timed out");
  return report;
}
}
QVariantMap clean(TimelineDocumentController& controller, const QStringList& categories) {
  QVariantMap result;
  bool finished = false;
  const auto connection = QObject::connect(&controller, &TimelineDocumentController::projectCacheCleanupFinished,
      &controller, [&](const QVariantMap& value) { result = value; finished = true; });
  const auto admission = controller.cleanupProjectCaches(categories);
  if (!admission.value("pending").toBool()) { QObject::disconnect(connection); return admission; }
  QElapsedTimer elapsed; elapsed.start();
  while (!finished && elapsed.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
  }
  QObject::disconnect(connection);
  require(finished, "Cleanup timed out");
  return result;
}

void contracts(TimelineDocumentController& controller, const QDir& root, const QString& documents) {
  const auto artifact = [&](int n) { return root.filePath(QStringLiteral("cache/timeline-posters/%1.png").arg(n)); };
  const auto document = [&](const QString& name, int n) {
    return write(QDir(documents).filePath(name), QJsonDocument(QJsonObject{{"timelineId", name},
        {"posterSource", artifact(n)}}).toJson(QJsonDocument::Compact));
  };
  // Rewrite a same-size document and restore its mtime. ctime/inode changes must
  // invalidate its index entry even when size and mtime are unchanged.
  require(document("mutation.json", 1998), "Mutation fixture failed");
  refresh(controller); auto report = await_report(controller);
  require(report.value("reclaimableFileCount").toInt() == 1499, "New document was not indexed");
  QFile changed(QDir(documents).filePath("mutation.json"));
  const auto timestamp = QFileInfo(changed).lastModified();
  require(document("mutation.json", 1999), "Replacement failed");
  require(changed.open(QIODevice::ReadWrite) && changed.setFileTime(timestamp, QFileDevice::FileModificationTime), "Timestamp fixture failed");
  changed.close();
  refresh(controller); report = await_report(controller);
  require(report.value("documentsRead").toInt() == 1, "Same-size restored-mtime rewrite reused stale reference data");
  // The report is now intentionally stale relative to another newly saved project.
  require(document("late.json", 1998), "Late shared reference fixture failed");
  const auto cleanup = clean(controller, {"thumbnail"});
  if (!cleanup.value("ok").toBool() || cleanup.value("removedFileCount").toInt() != 1498)
    std::cerr << QJsonDocument::fromVariant(cleanup).toJson().constData();
  require(cleanup.value("ok").toBool() && cleanup.value("removedFileCount").toInt() == 1498 &&
      cleanup.value("retainedSharedFileCount").toInt() == 502, "Cleanup did not revalidate shared references");
  require(QFileInfo::exists(artifact(1998)) && QFileInfo::exists(artifact(1999)) && !QFileInfo::exists(artifact(1997)), "Cleanup deleted a shared file");
  report = await_report(controller);
  require(report.value("totalFileCount").toInt() == 502, "Cleanup did not refresh the report");

  require(write(artifact(1997), QByteArray(256, 'c')), "Recreate failed");
  require(write(QDir(documents).filePath("unreadable.json"), "{broken"), "Malformed fixture failed");
  const auto blocked = clean(controller, {"thumbnail"});
  require(!blocked.value("ok").toBool() && QFileInfo::exists(artifact(1997)), "Unknown project references allowed deletion");
  QFile::remove(QDir(documents).filePath("unreadable.json"));

  // A generated path used as source media is retained, including an alias that
  // resolves through a symlinked directory outside the managed cache tree.
  TimelineHistoryTestAccess::protect(controller, artifact(1997));
  const auto protected_result = clean(controller, {"thumbnail"});
  require(protected_result.value("retainedProtectedFileCount").toInt() == 1 && QFileInfo::exists(artifact(1997)), "Source media was deleted");
  const auto outside = root.filePath("outside");
  require(QDir().mkpath(outside) && write(QDir(outside).filePath("source.png"), "original"), "Outside source fixture failed");
  require(QFile::link(outside, root.filePath("cache/timeline-posters/escape")), "Symlink fixture failed");
  TimelineHistoryTestAccess::poster(controller, root.filePath("cache/timeline-posters/escape/source.png"));
  const auto alias_result = clean(controller, {"thumbnail"});
  require(alias_result.value("retainedProtectedFileCount").toInt() >= 1 && QFileInfo::exists(QDir(outside).filePath("source.png")), "Managed-root alias escaped deletion guard");

  // A newer document replaces the immutable request while the worker is active.
  TimelineHistoryTestAccess::seed(controller, root.path(), 2000);
  refresh(controller); TimelineHistoryTestAccess::start(controller);
  TimelineHistoryTestAccess::seed(controller, root.path(), 2);
  TimelineHistoryTestAccess::rename(controller, "replacement");
  refresh(controller); report = await_report(controller);
  require(report.value("timelineId") == "replacement" && report.value("totalFileCount").toInt() == 2,
      "Late result replaced a newer document");

  auto closing = std::make_unique<TimelineDocumentController>();
  TimelineHistoryTestAccess::seed(*closing, root.path(), 2000);
  refresh(*closing); TimelineHistoryTestAccess::start(*closing);
  const auto future = TimelineHistoryTestAccess::future(*closing);
  QElapsedTimer destruction; destruction.start(); closing.reset();
  require(destruction.elapsed() < 100, "Controller destruction waited for disk inspection");
  QElapsedTimer wait; wait.start();
  while (!future.isFinished() && wait.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
  }
  require(future.isFinished(), "Detached inspection did not honor cancellation");
  TimelineDocumentController pressure_controller;
  TimelineHistoryTestAccess::seed(pressure_controller, root.path(), 1);
  const auto pressure_artifact = root.filePath("cache/timeline-posters/pressure.png");
  QFile pressure_file(pressure_artifact);
  require(pressure_file.open(QIODevice::WriteOnly) && pressure_file.resize(1024LL * 1024 * 1024 + 1),
      "Sparse pressure artifact failed");
  pressure_file.close();
  TimelineHistoryTestAccess::pressureArtifact(pressure_controller, pressure_artifact);
  pressure_controller.setCachePressureLevelForTesting("critical");
  const auto pressure_report = await_report(pressure_controller);
  require(!QFileInfo::exists(pressure_artifact) && pressure_report.value("totalFileCount").toInt() == 0,
      "Critical pressure did not perform background cleanup above the budget");
  pressure_controller.setCachePressureLevelForTesting("normal");
  std::cout << "Cache contracts passed: indexed mutation, fresh shared references, incomplete-scan protection, source/alias safety, stale result rejection, nonblocking destruction, and asynchronous pressure cleanup.\n";
}

int main(int argc, char** argv) try {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName("PhotoEditorTests");
  QCoreApplication::setApplicationName("TimelineCacheInspectionTest");
  QTemporaryDir temporary;
  require(temporary.isValid(), "Temporary directory failed");
  const QDir root(temporary.path());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_TIMELINE_DOCUMENTS_DIR", root.filePath("documents").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/cache_fixture");
  const int files = 2000, documents = 200, shared = 500;
  const auto document_root = timelineDocumentsRootPath();
  require(QDir().mkpath(document_root) && QDir().mkpath(root.filePath("cache/timeline-posters")), "Fixture folders failed");
  for (int i = 0; i < files; ++i)
    require(write(root.filePath(QStringLiteral("cache/timeline-posters/%1.png").arg(i)), QByteArray(256, 'c')), "Artifact creation failed");
  for (int i = 0; i < documents; ++i) {
    QJsonArray clips;
    for (int j = 0; j < 20; ++j) clips.append(QJsonObject{{"posterSource",root.filePath(
        QStringLiteral("cache/timeline-posters/%1.png").arg((i * 20 + j) % shared))}});
    require(write(QDir(document_root).filePath(QStringLiteral("other-%1.json").arg(i)), QJsonDocument(QJsonObject{
        {"timelineId", QStringLiteral("other-%1").arg(i)}, {"clips", clips}, {"label", QString(1024, 'x')}}).toJson()), "Document creation failed");
  }
  TimelineDocumentController controller;
  TimelineHistoryTestAccess::seed(controller, root.path(), files);
  QJsonArray samples;
  for (int i = 0; i < 8; ++i) {
    QElapsedTimer wall; wall.start();
    refresh(controller);
    const auto initial = controller.projectCacheSummary();
    const auto budget = controller.cacheBudgetState();
    Q_UNUSED(initial); Q_UNUSED(budget);
    const double getter_ms = static_cast<double>(wall.nsecsElapsed()) / 1e6;
    const auto report = await_report(controller);
    require(report.value("totalFileCount").toInt() == files && report.value("totalBytes").toLongLong() == files * 256 &&
        report.value("reclaimableFileCount").toInt() == files - shared, "Cache report counts differ");
    if (i > 0 && report.contains("documentsRead")) require(report.value("documentsRead").toInt() == 0, "Unchanged project JSON was reread");
    samples.append(QJsonObject{{"getter_ms", getter_ms}, {"ready_ms", double(wall.nsecsElapsed()) / 1e6},
        {"documents_read", report.value("documentsRead", -1).toInt()}, {"exact", true}});
  }
  controller.setCachePressureLevelForTesting("normal");
  QElapsedTimer pressure; pressure.start();
  controller.setCachePressureLevelForTesting("critical");
  const double pressure_ms = double(pressure.nsecsElapsed()) / 1e6;
  controller.setCachePressureLevelForTesting("normal");
  const QJsonObject output{{"samples", samples}, {"critical_pressure_ms", pressure_ms},
      {"files", files}, {"documents", documents}, {"scope", "Actual controller with generated files and saved project JSON; no native UI"}};
  if (app.arguments().contains("--contracts")) contracts(controller, root, document_root);
  else if (argc > 1) require(write(QString::fromLocal8Bit(argv[1]), QJsonDocument(output).toJson()), "Report write failed");
  std::cout << QJsonDocument(output).toJson().constData();
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
