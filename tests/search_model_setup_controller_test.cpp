#include "search_model_setup_controller.h"
#include "search_model_paths.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <iostream>

using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const auto app_data = root.filePath("organization/PhotoEditor");
  const auto shared_data = root.filePath("shared");
  const auto shared_model = QDir(shared_data).filePath("PhotoEditor/search-models/siglip2-base-patch16-224");
  const auto local_model = QDir(app_data).filePath("search-models/siglip2-base-patch16-224");
  if (!QDir().mkpath(shared_model) ||
      search_model_location(app_data, shared_data, {}, false) != shared_model ||
      search_model_location(app_data, shared_data, {}, true) != local_model ||
      search_model_location(app_data, shared_data, root.filePath("explicit"), false) != root.filePath("explicit") ||
      !QDir().mkpath(local_model) || search_model_location(app_data, shared_data, {}, false) != local_model)
    return 1;

  const auto wait = [](auto predicate) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(1);
    }
    return predicate();
  };
  std::atomic_bool valid{true}, off_thread{false};
  auto* owner_thread = QThread::currentThread();
  LocalSimilaritySearch search;
  SearchModelSetupController setup(&search, root.path(), "cpu", root.filePath("index.sqlite3"), nullptr,
      [&](const QString&, const SearchModelSetupController::State& state) {
        off_thread.store(QThread::currentThread() != owner_thread);
        QThread::msleep(30);
        return QVariantMap{{"ok", valid.load() && !state->canceled.load()},
                           {"error", "Model files are missing"}};
      });
  if (setup.download() || setup.available() || search.configured()) return 1;
  setup.setRuntime(QCoreApplication::applicationFilePath());
  if (!setup.running() || setup.ready() || search.configured() ||
      !wait([&] { return !setup.running(); }) || !setup.ready() ||
      !search.configured() || !off_thread.load()) return 1;
  valid.store(false);
  if (!setup.verify() || search.configured() ||
      !wait([&] { return !setup.running(); }) || setup.ready() || search.configured() ||
      !setup.status().contains("missing")) return 1;
  valid.store(true);
  if (!setup.verify() || !wait([&] { return !setup.running(); }) || !setup.ready() ||
      !search.configured()) return 1;
  if (setup.importFolder(QUrl("https://example.invalid/model")) || setup.download(false)) return 1;
  setup.verify();
  setup.cancel();
  if (!wait([&] { return !setup.running(); }) || setup.ready() || search.configured()) return 1;
  setup.shutdown();
  std::cout << "Search setup verifies off-thread, gates admission, recovers missing files, and cancels without admitting search\n";
  return 0;
}
