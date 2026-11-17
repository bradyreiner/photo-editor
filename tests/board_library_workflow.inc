int board_library_helper(int argc, char** argv) {
  const auto command = argc > 1 ? QString::fromUtf8(argv[1]) : QString{};
  const auto id = qEnvironmentVariable("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_ID");
  QFile file(QDir(qEnvironmentVariable("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH")).filePath("documents/" + id + ".json"));
  QJsonObject saved;
  if (file.open(QIODevice::ReadOnly)) saved = QJsonDocument::fromJson(file.readAll()).object();
  if (command == "list-summaries") {
    QFile marker(qEnvironmentVariable("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_CALLS"));
    if (marker.open(QIODevice::Append)) marker.write("list\n");
    QThread::msleep(250);
    const auto document = saved.value("document").toObject();
    const QJsonArray rows{QJsonObject{{"document_id", id}, {"name", saved.value("name")}, {"slide_count", 4},
        {"document_format", "custom"}, {"updated_at", saved.value("updatedAt")},
        {"document_json", QJsonObject{{"projectName", "Nepal 2026"}, {"documentArchived", false},
          {"documentSavedAtUtc", document.value("documentSavedAtUtc")}}}},
        QJsonObject{{"document_id", "another-project"}, {"name", "Other project"}, {"slide_count", 1},
          {"updated_at", "2026-01-01T00:00:00Z"}, {"document_json", QJsonObject{{"projectName", "Other"}}}}};
    std::cout << QJsonDocument(rows).toJson(QJsonDocument::Compact).constData();
    return 0;
  }
  if (command == "get" && argc > 2 && QString::fromUtf8(argv[2]) == id && !saved.isEmpty()) {
    std::cout << QJsonDocument(QJsonObject{{"document_id", id}, {"name", saved.value("name")},
        {"document_format", saved.value("documentFormat")}, {"slide_count", saved.value("slideCount")},
        {"updated_at", saved.value("updatedAt")},
        {"document_json", saved.value("document")}}).toJson(QJsonDocument::Compact).constData(); return 0;
  }
  return 1;
}

void verify_board_library(const QTemporaryDir& runtime) {
  const auto previous_root = qgetenv("PHOTO_EDITOR_REPOSITORY_ROOT"), previous_database = qgetenv("PHOTO_EDITOR_DATABASE_URL"),
      previous_documents = qgetenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH");
  const auto cleanup = qScopeGuard([&] {
    qunsetenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_HELPER"); qunsetenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_ID");
    qunsetenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_CALLS");
    qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", previous_root); qputenv("PHOTO_EDITOR_DATABASE_URL", previous_database);
    qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", previous_documents);
  });
  const auto helper_root = runtime.filePath("board-library-helper");
  require(QDir().mkpath(helper_root + "/rust/target/debug"), "Create isolated helper directory");
  QFile manifest(helper_root + "/rust/Cargo.toml");
  require(manifest.open(QIODevice::WriteOnly), "Create isolated helper manifest"); manifest.write("# isolated board listing fixture\n"); manifest.close();
  const auto helper = helper_root + "/rust/target/debug/catalog_carousel_documents";
  require(QFile::copy(QCoreApplication::applicationFilePath(), helper) && QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
          "Install the synthetic catalog helper");
  const auto calls_path = runtime.filePath("board-library-helper-calls.txt");
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "disabled-for-board-library-fixture");
  qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", runtime.filePath("board-library-documents").toUtf8());
  qputenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_HELPER", "1");
  qputenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_CALLS", calls_path.toUtf8());
  require(AppBootstrap::helperProgram("catalog_carousel_documents") == helper, "Listing tests must not access the user's catalog");
  CarouselDocumentController boards;
  require(boards.createEditCanvas({{"name", "Nepal 2026"}, {"projectName", "Nepal 2026"}, {"width", 120}, {"height", 80}}) &&
          boards.addEditBoard(false) && boards.addEditBoard(false) && boards.addEditBoard(false), "Save a four-board Nepal project document");
  qputenv("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_ID", boards.savedDocumentId().toUtf8());
  const auto wait = [](const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(2); }
    return ready();
  };
  int heartbeats = 0;
  QTimer heartbeat; heartbeat.setInterval(5);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; }); heartbeat.start();
  QElapsedTimer admission; admission.start(); boards.refreshRecentBoardDocuments();
  require(admission.elapsed() < 50 && boards.recentBoardDocumentsLoading(), "Opening Boards queues catalog work without blocking the GUI");
  require(wait([&] { return !boards.recentBoardDocumentsLoading(); }) && heartbeats >= 10, "The UI continues processing input while the helper is delayed");
  const auto rows = boards.recentBoardDocuments();
  bool found = false;
  for (const auto& value : rows) {
    const auto row = value.toMap();
    if (row.value("documentId") == boards.savedDocumentId()) found = row.value("projectName") == "Nepal 2026" && row.value("slideCount").toInt() == 4;
  }
  require(found, "Compact catalog metadata retains the Nepal 2026 project and all four boards");
  const auto call_count = [&] { QFile calls(calls_path); return calls.open(QIODevice::ReadOnly) ? calls.readAll().count('\n') : -1; };
  require(call_count() == 1, "The initial view uses one catalog list request");
  admission.restart(); boards.refreshRecentBoardDocuments();
  require(admission.elapsed() < 50 && !boards.recentBoardDocumentsLoading() && call_count() == 1, "Reopening Boards uses cached metadata immediately");
  boards.requestBoardThumbnail(boards.savedDocumentId());
  require(wait([&] { for (const auto& value : boards.recentBoardDocuments()) if (value.toMap().value("documentId") == boards.savedDocumentId())
          return !value.toMap().value("previewSource").toString().isEmpty(); return false; }), "A visible board loads its cover independently");
  require(call_count() == 1, "Thumbnail completion must not rerun the catalog list");
  require(boards.saveDocument("Nepal 2026 updated"), "Save while the Library cache exists");
  boards.refreshRecentBoardDocuments();
  require(wait([&] { return !boards.recentBoardDocumentsLoading(); }) && call_count() == 2, "Saving invalidates the cached summary exactly once");
  bool renamed = false;
  for (const auto& value : boards.recentBoardDocuments()) if (value.toMap().value("documentId") == boards.savedDocumentId())
    renamed = value.toMap().value("name") == "Nepal 2026 updated";
  require(renamed, "The project list reflects the latest saved board name");
  CarouselDocumentController reopened;
  const auto opened = reopened.loadDocument(boards.savedDocumentId());
  if (opened && reopened.documentContentHash() != boards.documentContentHash()) {
    const auto before = CarouselPreviewTestAccess::persistent(boards), after = CarouselPreviewTestAccess::persistent(reopened);
    for (const auto& key : after.keys()) if (before.value(key) != after.value(key))
      std::cerr << "Saved metadata difference " << key.toStdString() << ": "
                << QJsonDocument(QJsonArray{before.value(key), after.value(key)}).toJson(QJsonDocument::Compact).constData() << '\n';
  }
  require(opened && reopened.documentContentHash() == boards.documentContentHash(),
          "Storage timestamps and cached summaries must not change a saved board's authored content hash");
  QThreadPool::globalInstance()->waitForDone();
  std::cout << "Board Library: nonblocking load, project metadata, cache reuse, independent cover, and save invalidation passed\n";
}
