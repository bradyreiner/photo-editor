#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <iostream>
#include <memory>
#include <utility>

#include "library_workflow_controller.h"

class FakeJobQueue final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
  Q_PROPERTY(bool historyMutationsEnabled READ historyMutationsEnabled NOTIFY jobsChanged)

 public:
  explicit FakeJobQueue(QObject* parent = nullptr) : QObject(parent) {}

  [[nodiscard]] int jobCount() const { return jobs_.size(); }
  [[nodiscard]] bool historyMutationsEnabled() const { return history_mutations_enabled_; }

  void setJobs(QVariantList jobs) {
    jobs_ = std::move(jobs);
    emit jobsChanged();
  }

  void setHistoryMutationsEnabled(bool enabled) {
    if (history_mutations_enabled_ == enabled)
      return;
    history_mutations_enabled_ = enabled;
    emit jobsChanged();
  }

  [[nodiscard]] QStringList retriedJobIds() const { return retried_job_ids_; }
  [[nodiscard]] QStringList revealedJobIds() const { return revealed_job_ids_; }

  Q_INVOKABLE QVariantList jobsForCenter(const QString& job_kind = QString()) const {
    if (job_kind.trimmed().isEmpty())
      return jobs_;
    QVariantList result;
    for (const auto& value : jobs_) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobKind")).toString() == job_kind)
        result.push_back(value);
    }
    return result;
  }

  Q_INVOKABLE bool retryJob(const QString& job_id) {
    retried_job_ids_.push_back(job_id);
    return true;
  }

  Q_INVOKABLE bool revealJobOutput(const QString& job_id) {
    revealed_job_ids_.push_back(job_id);
    return true;
  }

 signals:
  void jobsChanged();

 private:
  QVariantList jobs_;
  QStringList retried_job_ids_;
  QStringList revealed_job_ids_;
  bool history_mutations_enabled_ = true;
};

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

void settle() {
  for (int index = 0; index < 14; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QVariantMap export_job(
    const QString& job_id,
    const QString& asset_id,
    const QString& status,
    const QString& output_path,
    bool verified,
    const QString& updated_at,
    const QString& error_message = QString()) {
  QVariantMap delivery_summary;
  delivery_summary.insert(QStringLiteral("outputPath"), output_path);
  delivery_summary.insert(QStringLiteral("artifactVerified"), verified);

  QVariantMap metadata;
  metadata.insert(QStringLiteral("deliverySummary"), delivery_summary);

  return {
      {QStringLiteral("jobId"), job_id},
      {QStringLiteral("jobKind"), QStringLiteral("asset_export")},
      {QStringLiteral("assetId"), asset_id},
      {QStringLiteral("displayLabel"), asset_id},
      {QStringLiteral("status"), status},
      {QStringLiteral("outputPath"), output_path},
      {QStringLiteral("errorMessage"), error_message},
      {QStringLiteral("updatedAt"), updated_at},
      {QStringLiteral("createdAt"), updated_at},
      {QStringLiteral("metadata"), metadata}};
}

QVariantMap timeline_job() {
  return {
      {QStringLiteral("jobId"), QStringLiteral("timeline-render")},
      {QStringLiteral("jobKind"), QStringLiteral("timeline_render")},
      {QStringLiteral("assetId"), QStringLiteral("asset-a")},
      {QStringLiteral("status"), QStringLiteral("Completed")},
      {QStringLiteral("outputPath"), QStringLiteral("/deliveries/timeline.mov")}};
}

QVariantMap mismatched_summary_path_job() {
  auto job = export_job(
      QStringLiteral("job-mismatched-summary-path"),
      QStringLiteral("asset-a"),
      QStringLiteral("Completed"),
      QStringLiteral("/deliveries/actual-output.jpg"),
      true,
      QStringLiteral("2026-10-07T16:30:00.000Z"));
  auto metadata = job.value(QStringLiteral("metadata")).toMap();
  auto summary = metadata.value(QStringLiteral("deliverySummary")).toMap();
  summary.insert(QStringLiteral("outputPath"), QStringLiteral("/deliveries/different-output.jpg"));
  metadata.insert(QStringLiteral("deliverySummary"), summary);
  job.insert(QStringLiteral("metadata"), metadata);
  return job;
}

QQuickItem* find_visual_child(QQuickItem* parent, const QString& object_name) {
  if (parent == nullptr)
    return nullptr;
  for (auto* child : parent->childItems()) {
    if (child->objectName() == object_name)
      return child;
    if (auto* nested = find_visual_child(child, object_name))
      return nested;
  }
  return nullptr;
}

QVariantList initial_jobs() {
  return {
      export_job(
          QStringLiteral("job-verified"),
          QStringLiteral("asset-a"),
          QStringLiteral("Completed"),
          QStringLiteral("/deliveries/portrait-a.jpg"),
          true,
          QStringLiteral("2026-10-07T10:00:00.000Z")),
      export_job(
          QStringLiteral("job-unverified"),
          QStringLiteral("asset-a"),
          QStringLiteral("Completed"),
          QStringLiteral("/deliveries/portrait-a-latest.jpg"),
          false,
          QStringLiteral("2026-10-07T11:00:00.000Z")),
      export_job(
          QStringLiteral("job-failed"),
          QStringLiteral("asset-b"),
          QStringLiteral("Failed"),
          QString(),
          false,
          QStringLiteral("2026-10-07T12:00:00.000Z"),
          QStringLiteral("The destination is full.")),
      export_job(
          QStringLiteral("job-queued"),
          QStringLiteral("asset-b"),
          QStringLiteral("Queued"),
          QString(),
          false,
          QStringLiteral("2026-10-07T13:00:00.000Z")),
      export_job(
          QStringLiteral("job-other-project"),
          QStringLiteral("asset-c"),
          QStringLiteral("Completed"),
          QStringLiteral("/deliveries/other-project.jpg"),
          true,
          QStringLiteral("2026-10-07T14:00:00.000Z")),
      timeline_job()};
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid())
    return fail("Workflow test storage should be available");
  photo_editor::desktop::LibraryWorkflowController workflow(
      temporary_directory.filePath(QStringLiteral("workflow.json")),
      QStringLiteral("delivery-panel-test"));
  if (!workflow.setProjectStage(QStringLiteral("Selected shoot"), QStringLiteral("ready")))
    return fail("The project workflow should accept its manual ready stage");

  FakeJobQueue queue;
  queue.setJobs(initial_jobs());

  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("fakeQueue"), &queue);
  engine.rootContext()->setContextProperty(QStringLiteral("workflowController"), &workflow);
  QQmlComponent component(&engine);
  component.setData(
      R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  id: fixture
  objectName: "libraryDeliveryFixture"
  width: 620
  height: 360
  visible: true
  color: "#171a1e"
  property var workflow: workflowController
  readonly property string projectStage: workflow.stageForProject("Selected shoot")
  property int exportRequestCount: 0

  LibraryDeliveryPanel {
    id: delivery
    objectName: "libraryDeliveryFixturePanel"
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.margins: 18
    width: 540
    height: implicitHeight
    jobQueue: fakeQueue
    assetIds: ["asset-a", "asset-b"]
    onExportSelectedRequested: fixture.exportRequestCount += 1
  }
})",
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/library-delivery-panel-fixture.qml")));

  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (window == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Library delivery panel fixture should instantiate");
  }

  auto* panel = window->findChild<QObject*>(QStringLiteral("libraryDeliveryFixturePanel"));
  auto* reveal = window->findChild<QQuickItem*>(QStringLiteral("libraryDeliveryRevealOutputButton"));
  auto* retry = qobject_cast<QQuickItem*>(
      find_visual_child(window->contentItem(), QStringLiteral("libraryDeliveryRetryButton")));
  auto* export_selected = window->findChild<QQuickItem*>(QStringLiteral("libraryDeliveryExportSelectedButton"));
  auto* expand = window->findChild<QQuickItem*>(QStringLiteral("libraryDeliveryExpandButton"));
  auto* collapsed_summary = qobject_cast<QQuickItem*>(
      find_visual_child(window->contentItem(), QStringLiteral("libraryDeliveryCollapsedSummary")));
  if (panel == nullptr || reveal == nullptr || retry == nullptr || export_selected == nullptr ||
      expand == nullptr || collapsed_summary == nullptr) {
    std::cerr << "Panel controls: panel=" << (panel != nullptr)
              << " reveal=" << (reveal != nullptr)
              << " retry=" << (retry != nullptr)
              << " export=" << (export_selected != nullptr)
              << " expand=" << (expand != nullptr)
              << " collapsedSummary=" << (collapsed_summary != nullptr) << '\n';
    return fail("Library delivery panel should expose its receipt and action controls");
  }

  window->show();
  // Production selection is a Qt QStringList, rather than a JavaScript Array.
  panel->setProperty("assetIds", QStringList{QStringLiteral("asset-a"), QStringLiteral("asset-b")});
  settle();

  if (panel->property("completedCount").toInt() != 2 ||
      panel->property("failedCount").toInt() != 1 ||
      panel->property("queuedCount").toInt() != 1 ||
      panel->property("projectJobs").toList().size() != 4) {
    std::cerr << "Receipt counts: complete=" << panel->property("completedCount").toInt()
              << " failed=" << panel->property("failedCount").toInt()
              << " queued=" << panel->property("queuedCount").toInt()
              << " scoped=" << panel->property("projectJobs").toList().size() << '\n';
    return fail("Counts should include only asset exports whose asset IDs belong to the selected project");
  }

  const auto proof_directory = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
  if (panel->property("expanded").toBool() || !collapsed_summary->isVisible())
    return fail("Delivery details should start collapsed while keeping the receipt counts visible");
  if (!proof_directory.isEmpty()) {
    if (!QDir().mkpath(proof_directory))
      return fail("Visual proof directory should be writable");
    if (!window->grabWindow().save(QDir(proof_directory).filePath(QStringLiteral("library-delivery-panel.png"))))
      return fail("Library delivery panel visual proof should save");
  }

  if (!QMetaObject::invokeMethod(expand, "clicked"))
    return fail("Delivery panel should expand through its disclosure control");
  settle();
  if (!panel->property("expanded").toBool() || !reveal->isVisible() || !retry->isVisible())
    return fail("Expanding delivery details should expose the verified receipt and failed-job retry");
  if (!proof_directory.isEmpty() &&
      !window->grabWindow().save(QDir(proof_directory).filePath(QStringLiteral("library-delivery-panel-expanded.png"))))
    return fail("Expanded library delivery panel proof should save");

  const auto verified = panel->property("lastVerifiedJob").toMap();
  if (verified.value(QStringLiteral("jobId")).toString() != QStringLiteral("job-verified") ||
      verified.value(QStringLiteral("outputPath")).toString() != QStringLiteral("/deliveries/portrait-a.jpg") ||
      !reveal->isVisible() || !reveal->isEnabled())
    return fail("The panel should choose a completed receipt with matching, verified output metadata");

  if (!QMetaObject::invokeMethod(reveal, "clicked") ||
      queue.revealedJobIds() != QStringList{QStringLiteral("job-verified")})
    return fail("Show folder should route through the real queue API with the verified job ID");

  if (!retry->isVisible() || !retry->isEnabled() ||
      !QMetaObject::invokeMethod(retry, "clicked") ||
      queue.retriedJobIds() != QStringList{QStringLiteral("job-failed")})
    return fail("A failed project export should expose an enabled retry routed to the queue API");

  queue.setHistoryMutationsEnabled(false);
  settle();
  if (retry->isEnabled())
    return fail("Retry should be disabled while job history is read-only");
  queue.setHistoryMutationsEnabled(true);
  settle();

  const auto stage_before_export = window->property("projectStage").toString();
  if (!export_selected->isVisible() || !export_selected->isEnabled() ||
      !QMetaObject::invokeMethod(export_selected, "clicked") ||
      window->property("exportRequestCount").toInt() != 1 ||
      window->property("projectStage").toString() != stage_before_export)
    return fail("Export selected should emit its handoff without changing the manually selected project stage");

  queue.setJobs({
      export_job(
          QStringLiteral("job-failed-only"),
          QStringLiteral("asset-a"),
          QStringLiteral("Failed"),
          QStringLiteral("/deliveries/failed-partial.jpg"),
          true,
          QStringLiteral("2026-10-07T14:30:00.000Z"),
          QStringLiteral("The export failed.")),
      export_job(
          QStringLiteral("job-queued-only"),
          QStringLiteral("asset-b"),
          QStringLiteral("Queued"),
          QStringLiteral("/deliveries/planned.jpg"),
          true,
          QStringLiteral("2026-10-07T14:45:00.000Z"))});
  settle();
  if (reveal->isVisible())
    return fail("Failed or queued exports must not expose a reveal action, even if metadata includes a path");

  queue.setJobs({
      export_job(
          QStringLiteral("job-only-unverified"),
          QStringLiteral("asset-a"),
          QStringLiteral("Completed"),
          QStringLiteral("/deliveries/unverified.jpg"),
          false,
          QStringLiteral("2026-10-07T15:00:00.000Z")),
      export_job(
          QStringLiteral("job-no-path"),
          QStringLiteral("asset-b"),
          QStringLiteral("Completed"),
          QString(),
          true,
          QStringLiteral("2026-10-07T16:00:00.000Z")),
      mismatched_summary_path_job(),
      export_job(
          QStringLiteral("job-foreign"),
          QStringLiteral("asset-c"),
          QStringLiteral("Completed"),
          QStringLiteral("/deliveries/foreign.jpg"),
          true,
          QStringLiteral("2026-10-07T17:00:00.000Z"))});
  settle();
  if (panel->property("completedCount").toInt() != 3 || reveal->isVisible())
    return fail("Completed jobs without a verified actual output path must not expose Reveal");

  panel->setProperty("assetIds", QVariantList{});
  settle();
  if (panel->property("completedCount").toInt() != 0 ||
      panel->property("failedCount").toInt() != 0 ||
      panel->property("queuedCount").toInt() != 0 || export_selected->isVisible())
    return fail("An empty project asset selection should clear counts and hide its export action");

  std::cout << "Library delivery panel: project receipt filtering, verified reveal, queue retry/read-only behavior, export handoff, and manual stage preservation passed.\n";
  return 0;
}

#include "library_delivery_panel_ui_test.moc"
