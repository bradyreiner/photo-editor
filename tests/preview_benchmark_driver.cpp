#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "observability.h"

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QString option_value(const QStringList& arguments, const QString& name) {
  const auto index = arguments.indexOf(name);
  if (index < 0 || index + 1 >= arguments.size()) {
    return {};
  }
  return arguments.at(index + 1).trimmed();
}

bool wait_for_preview_idle(
    photo_editor::desktop::EditorPreviewController& controller,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (!controller.processing()) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
      return true;
    }
    QThread::msleep(25);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return !controller.processing();
}

QString read_text_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

class BenchmarkImageSink final : public photo_editor::desktop::EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    Q_UNUSED(key);
    last_image_size_ = image.size();
  }

  void clearImage(const QString& key) override {
    Q_UNUSED(key);
  }

  [[nodiscard]] QSize lastImageSize() const { return last_image_size_; }

 private:
  QSize last_image_size_;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("PreviewBenchmarkDriver"));
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));

  const auto arguments = QCoreApplication::arguments();
  const auto corpus_path = option_value(arguments, QStringLiteral("--corpus"));
  const auto limit_value = option_value(arguments, QStringLiteral("--limit"));
  const auto timeout_value = option_value(arguments, QStringLiteral("--timeout-ms"));
  const auto limit = limit_value.isEmpty() ? 0 : limit_value.toInt();
  const auto timeout_ms = timeout_value.isEmpty() ? 15000 : timeout_value.toInt();
  if (corpus_path.isEmpty()) {
    return fail(QStringLiteral("Usage: preview_benchmark_driver --corpus <manifest.json> [--limit N]"));
  }

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.ready"),
      {
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("benchmark_driver"), QStringLiteral("preview")},
      });

  QFile manifest_file(corpus_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("Unable to open corpus manifest: %1").arg(corpus_path));
  }
  const auto manifest_document = QJsonDocument::fromJson(manifest_file.readAll());
  if (!manifest_document.isObject()) {
    return fail(QStringLiteral("Corpus manifest must be a JSON object."));
  }
  const auto assets = manifest_document.object().value(QStringLiteral("assets")).toArray();
  if (assets.isEmpty()) {
    return fail(QStringLiteral("Corpus manifest contains no assets."));
  }

  BenchmarkImageSink image_sink;
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&image_sink);
  controller.setPreviewTargetEdge(960);

  QElapsedTimer total_timer;
  total_timer.start();
  auto rendered_count = 0;
  for (const auto& value : assets) {
    if (!value.isObject()) {
      continue;
    }
    if (limit > 0 && rendered_count >= limit) {
      break;
    }
    const auto asset = value.toObject();
    const auto asset_id = asset.value(QStringLiteral("asset_id")).toString().trimmed();
    const auto file_path = asset.value(QStringLiteral("path")).toString().trimmed();
    const auto recipe_path = asset.value(QStringLiteral("recipe")).toString().trimmed();
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }

    const auto recipe_json = recipe_path.isEmpty() ? QString() : read_text_file(recipe_path);
    QElapsedTimer asset_timer;
    asset_timer.start();
    controller.loadAsset(asset_id, file_path, QString(), QString(), QString(), QString(), recipe_json);
    if (!wait_for_preview_idle(controller, timeout_ms)) {
      return fail(QStringLiteral("Preview render timed out for %1").arg(asset_id));
    }
    ++rendered_count;
    const auto image_size = image_sink.lastImageSize();
    photo_editor::desktop::observability::logInfo(
        photoEditorPerfLog(),
        QStringLiteral("benchmark.preview_asset.completed"),
        {
            {QStringLiteral("asset_id"), asset_id},
            {QStringLiteral("file_path"), file_path},
            {QStringLiteral("elapsed_ms"), asset_timer.elapsed()},
            {QStringLiteral("image_width"), image_size.width()},
            {QStringLiteral("image_height"), image_size.height()},
        });
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.preview_corpus.completed"),
      {
          {QStringLiteral("corpus"), corpus_path},
          {QStringLiteral("asset_count"), rendered_count},
          {QStringLiteral("elapsed_ms"), total_timer.elapsed()},
      });
  std::cout << "rendered_assets=" << rendered_count << '\n';
  return rendered_count > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
