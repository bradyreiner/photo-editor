#include <cstdlib>
#include <iostream>
#include <algorithm>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
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

QString read_text_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

QVector<photo_editor::desktop::CatalogAssetItem> load_assets(
    const QJsonArray& manifest_assets,
    int limit) {
  QVector<photo_editor::desktop::CatalogAssetItem> items;
  items.reserve(limit > 0 ? std::min(limit, static_cast<int>(manifest_assets.size()))
                          : static_cast<int>(manifest_assets.size()));

  for (const auto& value : manifest_assets) {
    if (!value.isObject()) {
      continue;
    }
    if (limit > 0 && items.size() >= limit) {
      break;
    }

    const auto asset = value.toObject();
    const auto asset_id = asset.value(QStringLiteral("asset_id")).toString().trimmed();
    const auto file_path = asset.value(QStringLiteral("path")).toString().trimmed();
    if (asset_id.isEmpty() || file_path.isEmpty()) {
      continue;
    }

    photo_editor::desktop::CatalogAssetItem item;
    item.asset_id = asset_id;
    item.source_asset_id = asset_id;
    item.file_path = file_path;
    item.location_path = file_path;
    item.media_type = QStringLiteral("photo");
    item.camera_label = QStringLiteral("Synthetic Benchmark");
    item.capture_date = QStringLiteral("May 24, 2026");
    item.imported_at = QStringLiteral("May 24, 2026  12:00 PM");
    item.online = true;
    const auto recipe_path = asset.value(QStringLiteral("recipe")).toString().trimmed();
    item.adjustment_patch_json =
        recipe_path.isEmpty()
            ? QString()
            : photo_editor::desktop::
                  canonical_asset_bound_adjustment_patch_json(
                      read_text_file(recipe_path));
    items.push_back(std::move(item));
  }

  return items;
}

bool wait_for_thumbnail_window(
    photo_editor::desktop::CatalogAssetModel* model,
    int count,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();

  while (timer.elapsed() < timeout_ms) {
    auto ready_count = 0;
    for (int row = 0; row < count; ++row) {
      const auto thumbnail = model->data(
          model->index(row, 0),
          photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
      if (!thumbnail.isEmpty()) {
        ++ready_count;
      }
    }
    if (ready_count >= count) {
      return true;
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(25);
  }

  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return false;
}

int ready_thumbnail_count(photo_editor::desktop::CatalogAssetModel* model, int count) {
  auto ready_count = 0;
  for (int row = 0; row < count; ++row) {
    const auto thumbnail = model->data(
        model->index(row, 0),
        photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
    if (!thumbnail.isEmpty()) {
      ++ready_count;
    }
  }
  return ready_count;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorBenchmarks"));
  QCoreApplication::setApplicationName(QStringLiteral("ThumbnailBenchmarkDriver"));

  const auto arguments = QCoreApplication::arguments();
  const auto corpus_path = option_value(arguments, QStringLiteral("--corpus"));
  const auto limit_value = option_value(arguments, QStringLiteral("--limit"));
  const auto window_value = option_value(arguments, QStringLiteral("--window-size"));
  const auto timeout_value = option_value(arguments, QStringLiteral("--timeout-ms"));
  const auto limit = limit_value.isEmpty() ? 0 : limit_value.toInt();
  const auto requested_window_size = window_value.isEmpty() ? 24 : window_value.toInt();
  const auto timeout_ms = timeout_value.isEmpty() ? 15000 : timeout_value.toInt();
  if (corpus_path.isEmpty()) {
    return fail(QStringLiteral(
        "Usage: thumbnail_benchmark_driver --corpus <manifest.json> [--limit N] [--window-size N]"));
  }

  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.ready"),
      {
          {QStringLiteral("elapsed_ms"), 0},
          {QStringLiteral("benchmark_driver"), QStringLiteral("thumbnail")},
      });

  QFile manifest_file(corpus_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("Unable to open corpus manifest: %1").arg(corpus_path));
  }
  const auto manifest_document = QJsonDocument::fromJson(manifest_file.readAll());
  if (!manifest_document.isObject()) {
    return fail(QStringLiteral("Corpus manifest must be a JSON object."));
  }

  const auto items = load_assets(
      manifest_document.object().value(QStringLiteral("assets")).toArray(),
      limit);
  if (items.isEmpty()) {
    return fail(QStringLiteral("Corpus manifest contains no usable assets."));
  }
  const auto window_size = std::clamp(requested_window_size, 1, static_cast<int>(items.size()));

  QElapsedTimer cold_timer;
  cold_timer.start();
  photo_editor::desktop::CatalogAssetModel cold_model;
  cold_model.replaceAssetsForTesting(items);
  cold_model.setVisibleAssetRange(0, window_size - 1);
  if (!wait_for_thumbnail_window(&cold_model, window_size, timeout_ms)) {
    return fail(QStringLiteral("Cold thumbnail benchmark timed out."));
  }
  const auto cold_elapsed_ms = cold_timer.elapsed();
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.thumbnail_corpus.completed"),
      {
          {QStringLiteral("corpus"), corpus_path},
          {QStringLiteral("pass"), QStringLiteral("cold")},
          {QStringLiteral("asset_count"), static_cast<int>(items.size())},
          {QStringLiteral("window_size"), window_size},
          {QStringLiteral("ready_count"), ready_thumbnail_count(&cold_model, window_size)},
          {QStringLiteral("elapsed_ms"), cold_elapsed_ms},
      });

  QElapsedTimer warm_timer;
  warm_timer.start();
  photo_editor::desktop::CatalogAssetModel warm_model;
  warm_model.replaceAssetsForTesting(items);
  warm_model.setVisibleAssetRange(0, window_size - 1);
  if (!wait_for_thumbnail_window(&warm_model, window_size, timeout_ms)) {
    return fail(QStringLiteral("Warm thumbnail benchmark timed out."));
  }
  const auto warm_ready_count = ready_thumbnail_count(&warm_model, window_size);
  const auto warm_elapsed_ms = warm_timer.elapsed();
  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.thumbnail_corpus.completed"),
      {
          {QStringLiteral("corpus"), corpus_path},
          {QStringLiteral("pass"), QStringLiteral("warm")},
          {QStringLiteral("asset_count"), static_cast<int>(items.size())},
          {QStringLiteral("window_size"), window_size},
          {QStringLiteral("ready_count"), warm_ready_count},
          {QStringLiteral("elapsed_ms"), warm_elapsed_ms},
      });

  if (warm_ready_count < window_size) {
    return fail(QStringLiteral("Warm thumbnail benchmark did not find cached thumbnails."));
  }

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("benchmark.thumbnail_corpus.summary_completed"),
      {
          {QStringLiteral("corpus"), corpus_path},
          {QStringLiteral("asset_count"), static_cast<int>(items.size())},
          {QStringLiteral("window_size"), window_size},
          {QStringLiteral("cold_elapsed_ms"), cold_elapsed_ms},
          {QStringLiteral("warm_elapsed_ms"), warm_elapsed_ms},
          {QStringLiteral("warm_ready_count"), warm_ready_count},
          {QStringLiteral("elapsed_ms"), cold_elapsed_ms + warm_elapsed_ms},
      });

  std::cout << "thumbnail_assets=" << items.size() << '\n';
  std::cout << "window_size=" << window_size << '\n';
  std::cout << "cold_elapsed_ms=" << cold_elapsed_ms << '\n';
  std::cout << "warm_elapsed_ms=" << warm_elapsed_ms << '\n';
  return EXIT_SUCCESS;
}
