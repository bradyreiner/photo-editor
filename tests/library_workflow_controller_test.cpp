#include "library_workflow_controller.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using photo_editor::desktop::LibraryWorkflowController;

class LibraryWorkflowControllerTest final : public QObject {
  Q_OBJECT

 private slots:
  void persistsPerCatalogWorkflowAndPreservesOtherData();
  void savedSearchActivationAndDeletionSurviveReopen();
  void malformedStateIsNeverReplaced();
  void staleWindowsMergeTheirChanges();
};

void LibraryWorkflowControllerTest::staleWindowsMergeTheirChanges() {
  QTemporaryDir temporary;
  const auto path = temporary.filePath(QStringLiteral("workflow.json"));
  LibraryWorkflowController first(path, QStringLiteral("catalog"));
  LibraryWorkflowController second(path, QStringLiteral("catalog"));
  LibraryWorkflowController other(path, QStringLiteral("other"));
  QVERIFY(first.setProjectStage(QStringLiteral("Shoot A"), QStringLiteral("edit")));
  QVERIFY(second.setProjectStage(QStringLiteral("Shoot B"), QStringLiteral("ready")));
  QVERIFY(other.setProjectStage(QStringLiteral("Shoot C"), QStringLiteral("archived")));
  const QVariantMap filters{{QStringLiteral("reviewFilter"), QStringLiteral("picked")}};
  QVERIFY(first.saveSearch(QStringLiteral("Search A"), filters));
  const auto first_id = first.savedSearches().first().toMap().value(QStringLiteral("id")).toString();
  QVERIFY(second.saveSearch(QStringLiteral("Search B"), filters));
  QVERIFY(first.deleteSearch(first_id));
  LibraryWorkflowController reopened(path, QStringLiteral("catalog"));
  QCOMPARE(reopened.stageForProject(QStringLiteral("Shoot A")), QStringLiteral("edit"));
  QCOMPARE(reopened.stageForProject(QStringLiteral("Shoot B")), QStringLiteral("ready"));
  QCOMPARE(reopened.savedSearches().size(), 1);
  QCOMPARE(reopened.savedSearches().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Search B"));
  LibraryWorkflowController reopened_other(path, QStringLiteral("other"));
  QCOMPARE(reopened_other.stageForProject(QStringLiteral("Shoot C")), QStringLiteral("archived"));
  QLockFile held_lock(path + QStringLiteral(".lock"));
  QVERIFY(held_lock.tryLock(0));
  QVERIFY(!first.setProjectStage(QStringLiteral("Shoot A"), QStringLiteral("delivered")));
  QCOMPARE(first.stageForProject(QStringLiteral("Shoot A")), QStringLiteral("edit"));
}

void LibraryWorkflowControllerTest::persistsPerCatalogWorkflowAndPreservesOtherData() {
  QTemporaryDir temporary_directory;
  QVERIFY(temporary_directory.isValid());
  const auto state_path = temporary_directory.filePath(QStringLiteral("library-workflow.json"));
  const QJsonObject initial_document{
      {QStringLiteral("otherFeature"), QJsonObject{{QStringLiteral("keep"), true}}},
      {QStringLiteral("libraryWorkflow"), QJsonObject{
          {QStringLiteral("version"), 1},
          {QStringLiteral("unknownWorkflowSetting"), QStringLiteral("preserve")},
          {QStringLiteral("scopes"), QJsonObject{
              {QStringLiteral("catalog-other"), QJsonObject{{QStringLiteral("value"), 42}}},
              {QStringLiteral("catalog-main"), QJsonObject{{QStringLiteral("otherScopeField"), QStringLiteral("keep")}}}}}}}};
  QFile initial_file(state_path);
  QVERIFY(initial_file.open(QIODevice::WriteOnly));
  QCOMPARE(initial_file.write(QJsonDocument(initial_document).toJson()),
           QJsonDocument(initial_document).toJson().size());
  initial_file.close();

  const QVariantMap filters{
      {QStringLiteral("searchFilter"), QStringLiteral("mountain")},
      {QStringLiteral("projectFilter"), QStringLiteral("Nepal 2026")},
      {QStringLiteral("reviewFilter"), QStringLiteral("picked")},
      {QStringLiteral("minimumRatingFilter"), 4},
      {QStringLiteral("sortOrder"), QStringLiteral("capture-asc")}};
  QString search_id;
  {
    LibraryWorkflowController controller(state_path, QStringLiteral("catalog-main"));
    QVERIFY(controller.persistenceError().isEmpty());
    QCOMPARE(controller.stageForProject(QStringLiteral("Nepal 2026")), QStringLiteral("review"));
    QVERIFY(controller.setProjectStage(QStringLiteral("Nepal 2026"), QStringLiteral("ready")));
    QVERIFY(controller.setProjectStage(QStringLiteral("Client selects"), QStringLiteral("delivered")));
    QVERIFY(!controller.setProjectStage(QStringLiteral("Client selects"), QStringLiteral("exported")));
    controller.setNavigationCollapsed(true);
    QVERIFY(controller.navigationCollapsed());

    QSignalSpy project_requests(&controller, &LibraryWorkflowController::projectRequested);
    controller.requestProject(QStringLiteral("Nepal 2026"));
    QCOMPARE(project_requests.size(), 1);
    QCOMPARE(project_requests.takeFirst().at(0).toString(), QStringLiteral("Nepal 2026"));

    QVERIFY(controller.saveSearch(QStringLiteral("  Keepers  "), filters));
    QCOMPARE(controller.savedSearches().size(), 1);
    const auto saved = controller.savedSearches().first().toMap();
    search_id = saved.value(QStringLiteral("id")).toString();
    QVERIFY(!search_id.isEmpty());
    QVERIFY(QDateTime::fromString(saved.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs).isValid());
    QCOMPARE(saved.value(QStringLiteral("name")).toString(), QStringLiteral("Keepers"));
    QCOMPARE(saved.value(QStringLiteral("filters")).toMap(), filters);
    QVERIFY(!controller.saveSearch(QStringLiteral("keepers"), filters));
  }

  {
    LibraryWorkflowController reopened(state_path, QStringLiteral("catalog-main"));
    QVERIFY(reopened.persistenceError().isEmpty());
    QVERIFY(reopened.navigationCollapsed());
    QCOMPARE(reopened.activeDestination(), QStringLiteral("project:Nepal 2026"));
    QCOMPARE(reopened.stageForProject(QStringLiteral("Nepal 2026")), QStringLiteral("ready"));
    QCOMPARE(reopened.stageForProject(QStringLiteral("Client selects")), QStringLiteral("delivered"));
    QCOMPARE(reopened.savedSearches().size(), 1);
    QCOMPARE(reopened.savedSearches().first().toMap().value(QStringLiteral("filters")).toMap(), filters);
  }

  {
    LibraryWorkflowController other_catalog(state_path, QStringLiteral("catalog-other"));
    QVERIFY(other_catalog.persistenceError().isEmpty());
    QVERIFY(!other_catalog.navigationCollapsed());
    QCOMPARE(other_catalog.stageForProject(QStringLiteral("Nepal 2026")), QStringLiteral("review"));
    QVERIFY(other_catalog.savedSearches().isEmpty());
  }

  QFile persisted_file(state_path);
  QVERIFY(persisted_file.open(QIODevice::ReadOnly));
  const auto persisted = QJsonDocument::fromJson(persisted_file.readAll()).object();
  QVERIFY(persisted.value(QStringLiteral("otherFeature")).toObject().value(QStringLiteral("keep")).toBool());
  const auto workflow = persisted.value(QStringLiteral("libraryWorkflow")).toObject();
  QCOMPARE(workflow.value(QStringLiteral("unknownWorkflowSetting")).toString(), QStringLiteral("preserve"));
  const auto scopes = workflow.value(QStringLiteral("scopes")).toObject();
  QCOMPARE(scopes.value(QStringLiteral("catalog-other")).toObject().value(QStringLiteral("value")).toInt(), 42);
  QCOMPARE(scopes.value(QStringLiteral("catalog-main")).toObject().value(QStringLiteral("otherScopeField")).toString(),
           QStringLiteral("keep"));
}

void LibraryWorkflowControllerTest::savedSearchActivationAndDeletionSurviveReopen() {
  QTemporaryDir temporary_directory;
  QVERIFY(temporary_directory.isValid());
  const auto state_path = temporary_directory.filePath(QStringLiteral("library-workflow.json"));
  const QVariantMap filters{
      {QStringLiteral("searchFilter"), QStringLiteral("client selects")},
      {QStringLiteral("availabilityFilter"), QStringLiteral("online")},
      {QStringLiteral("sortOrder"), QStringLiteral("rating-desc")}};

  QString search_id;
  {
    LibraryWorkflowController controller(state_path, QStringLiteral("catalog-A"));
    QVERIFY(controller.saveSearch(QStringLiteral("Online selects"), filters));
    search_id = controller.savedSearches().first().toMap().value(QStringLiteral("id")).toString();
    QSignalSpy search_requests(&controller, &LibraryWorkflowController::savedSearchRequested);
    QVERIFY(controller.activateSavedSearch(search_id));
    QCOMPARE(search_requests.size(), 1);
    const auto request = search_requests.takeFirst();
    QCOMPARE(request.at(0).toString(), search_id);
    QCOMPARE(request.at(1).toMap(), filters);
    QCOMPARE(controller.activeDestination(), QStringLiteral("saved-search:") + search_id);
  }

  {
    LibraryWorkflowController reopened(state_path, QStringLiteral("catalog-A"));
    QCOMPARE(reopened.activeDestination(), QStringLiteral("saved-search:") + search_id);
    QVERIFY(reopened.deleteSearch(search_id));
    QCOMPARE(reopened.savedSearches().size(), 0);
    QCOMPARE(reopened.activeDestination(), QStringLiteral("all-images"));
  }
  {
    LibraryWorkflowController after_delete(state_path, QStringLiteral("catalog-A"));
    QCOMPARE(after_delete.activeDestination(), QStringLiteral("all-images"));
    QVERIFY(after_delete.savedSearches().isEmpty());
  }
}

void LibraryWorkflowControllerTest::malformedStateIsNeverReplaced() {
  QTemporaryDir temporary_directory;
  QVERIFY(temporary_directory.isValid());
  const auto state_path = temporary_directory.filePath(QStringLiteral("library-workflow.json"));
  const QByteArray malformed("{not-json\nkeep this file");
  QFile file(state_path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(malformed), malformed.size());
  file.close();

  LibraryWorkflowController controller(state_path, QStringLiteral("catalog-A"));
  QVERIFY(!controller.persistenceError().isEmpty());
  QVERIFY(!controller.setProjectStage(QStringLiteral("Shoot"), QStringLiteral("ready")));
  QVERIFY(!controller.saveSearch(QStringLiteral("Test"), {{QStringLiteral("sortOrder"), QStringLiteral("filename-asc")}}));
  controller.setNavigationCollapsed(true);
  QVERIFY(controller.navigationCollapsed());

  QFile preserved_file(state_path);
  QVERIFY(preserved_file.open(QIODevice::ReadOnly));
  QCOMPARE(preserved_file.readAll(), malformed);
}

QTEST_GUILESS_MAIN(LibraryWorkflowControllerTest)
#include "library_workflow_controller_test.moc"
