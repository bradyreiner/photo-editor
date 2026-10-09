#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "catalog_metadata_io.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <functional>
#include <iostream>
#include <memory>

using namespace photo_editor::desktop;

namespace {

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool write_file(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QJsonObject read_object(const QString& path) {
  return QJsonDocument::fromJson(read_file(path)).object();
}

bool wait_until(const std::function<bool()>& ready, int timeout_ms = 15000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}

int helper(const QStringList& arguments) {
  const QDir root(qEnvironmentVariable("PHOTO_EDITOR_METADATA_FIXTURE_ROOT"));
  const auto command = arguments.value(1);
  if (command == QStringLiteral("0")) {
    if (QFileInfo::exists(root.filePath(QStringLiteral("hold-snapshot"))) &&
        !wait_until([&] { return QFileInfo::exists(root.filePath(QStringLiteral("release-snapshot"))); }, 20000)) {
      return 3;
    }
    const auto bytes = read_file(root.filePath(QStringLiteral("snapshot.json")));
    std::cout.write(bytes.constData(), bytes.size());
    return 0;
  }
  if (command != QStringLiteral("set-asset-metadata") || arguments.size() < 3) return 2;

  QJsonParseError parse_error;
  const auto request = QJsonDocument::fromJson(read_file(arguments.at(2)), &parse_error).object();
  const auto records = request.value(QStringLiteral("records")).toArray();
  if (parse_error.error != QJsonParseError::NoError || records.isEmpty() || records.size() > 256) return 2;
  const int call = read_file(root.filePath(QStringLiteral("metadata-calls"))).toInt() + 1;
  if (!write_file(root.filePath(QStringLiteral("metadata-request-%1.json").arg(call)),
                  QJsonDocument(request).toJson(QJsonDocument::Compact)) ||
      !write_file(root.filePath(QStringLiteral("metadata-calls")), QByteArray::number(call))) {
    return 2;
  }
  const auto release_path = root.filePath(QStringLiteral("metadata-release-%1").arg(call));
  if (QFileInfo::exists(root.filePath(QStringLiteral("hold-metadata"))) &&
      !wait_until([&] { return QFileInfo::exists(release_path); }, 20000)) {
    return 3;
  }
  if (QFileInfo::exists(root.filePath(QStringLiteral("metadata-fail-%1").arg(call)))) return 4;

  const auto missing_lines = QString::fromUtf8(read_file(root.filePath(QStringLiteral("metadata-missing-ids"))))
                                 .split('\n', Qt::SkipEmptyParts);
  auto store = read_object(root.filePath(QStringLiteral("catalog-metadata.json")));
  QJsonArray applied;
  QJsonArray missing;
  for (const auto& value : records) {
    const auto record = value.toObject();
    const auto id = record.value(QStringLiteral("asset_id")).toString();
    if (id.isEmpty()) return 2;
    if (missing_lines.contains(id)) {
      missing.append(id);
    } else {
      store.insert(id, record.value(QStringLiteral("metadata")));
      applied.append(id);
    }
  }
  if (!write_file(root.filePath(QStringLiteral("catalog-metadata.json")),
                  QJsonDocument(store).toJson(QJsonDocument::Compact))) {
    return 2;
  }
  if (QFileInfo::exists(root.filePath(QStringLiteral("metadata-bad-receipt-%1").arg(call)))) {
    std::cout << "not a receipt";
    return 0;
  }
  const auto receipt = QJsonDocument(QJsonObject{
      {QStringLiteral("applied"), applied}, {QStringLiteral("missing"), missing}})
                           .toJson(QJsonDocument::Compact);
  std::cout.write(receipt.constData(), receipt.size());
  return 0;
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

struct GlobalPoolGate final {
  QThreadPool* pool = QThreadPool::globalInstance();
  int previous_maximum = 0;
  QSemaphore started;
  QSemaphore release;
  QFuture<void> future;
  bool ready = false;

  GlobalPoolGate() {
    pool->waitForDone();
    previous_maximum = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    future = QtConcurrent::run([this] {
      started.release();
      release.acquire();
    });
    ready = wait_until([this] { return started.available() > 0; });
    if (ready) started.acquire();
  }

  ~GlobalPoolGate() {
    release.release();
    future.waitForFinished();
    pool->setMaxThreadCount(previous_maximum);
  }
};

QVector<CatalogAssetItem> fixture_assets(const QString& root_path) {
  QVector<CatalogAssetItem> items;
  for (const auto& pair : {qMakePair(QStringLiteral("asset-a"), QStringLiteral("Frame.CR3")),
                           qMakePair(QStringLiteral("asset-b"), QStringLiteral("frame.JPG"))}) {
    CatalogAssetItem item;
    item.asset_id = pair.first;
    item.file_path = QDir(root_path).filePath(pair.second);
    item.media_type = QStringLiteral("photo");
    item.online = true;
    items.push_back(item);
  }
  return items;
}

bool write_snapshot(const QString& root_path, const QVariantMap& metadata_a, const QVariantMap& metadata_b) {
  const auto items = fixture_assets(root_path);
  QJsonArray assets;
  for (const auto& item : items) {
    assets.append(QJsonObject{
        {QStringLiteral("asset_id"), item.asset_id},
        {QStringLiteral("media_type"), QStringLiteral("photo")},
        {QStringLiteral("location"), QJsonObject{
            {QStringLiteral("root_path"), root_path},
            {QStringLiteral("relative_path"), QFileInfo(item.file_path).fileName()},
            {QStringLiteral("online"), true}}}});
  }
  const QJsonObject metadata{
      {QStringLiteral("asset-a"), QJsonObject::fromVariantMap(metadata_a)},
      {QStringLiteral("asset-b"), QJsonObject::fromVariantMap(metadata_b)}};
  const auto snapshot = QJsonDocument(QJsonObject{
      {QStringLiteral("assets"), assets},
      {QStringLiteral("drives"), QJsonArray{}},
      {QStringLiteral("projects"), QJsonArray{}},
      {QStringLiteral("review_states"), QJsonObject{}},
      {QStringLiteral("relinked_paths"), QJsonObject{}},
      {QStringLiteral("asset_metadata"), metadata}}).toJson(QJsonDocument::Compact);
  return write_file(QDir(root_path).filePath(QStringLiteral("snapshot.json")), snapshot);
}

QVariantMap outbox_record(const QString& app_data_path, const QString& asset_id) {
  const auto root = read_object(QDir(app_data_path).filePath(QStringLiteral("metadata-outbox.json")));
  return root.value(QStringLiteral("records")).toObject().value(asset_id).toObject().toVariantMap();
}

bool outbox_empty(const QString& app_data_path) {
  const auto path = QDir(app_data_path).filePath(QStringLiteral("metadata-outbox.json"));
  if (!QFileInfo::exists(path)) return true;
  return read_object(path).value(QStringLiteral("records")).toObject().isEmpty();
}

int run_contracts(const QString& root_path) {
  const QDir root(root_path);
  const auto app_data = root.filePath(QStringLiteral("data"));
  const auto outbox_path = QDir(app_data).filePath(QStringLiteral("metadata-outbox.json"));
  const auto items = fixture_assets(root_path);
  if (!QDir().mkpath(app_data)) return fail("Could not create app data fixture");
  for (const auto& item : items) {
    if (!write_file(item.file_path, QByteArray("photo fixture"))) return fail("Could not create media fixture");
  }
  if (!write_snapshot(root_path, {}, {})) return fail("Could not create catalog snapshot fixture");

  auto model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  {
    CatalogAssetModel competing_window;
    competing_window.setInteractiveRenderingActive(true);
    competing_window.replaceAssetsForTesting(items);
    competing_window.selectIndex(0);
    if (competing_window.saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Conflicting window")}}))
      return fail("A competing window must not replace this window's metadata outbox");
  }
  // Capture-time groups must not depend on catalog/import ordering or on the
  // shared filename prefix of every photo shot that day.
  auto timed = QVector<CatalogAssetItem>{items[0], items[1], items[0]};
  const QStringList times{"2026-10-07T12:00:01.300-06:00", "2026-10-07T14:30:00-06:00", "2026-10-07T12:00:00.100-06:00"};
  for (qsizetype index = 0; index < timed.size(); ++index) {
    timed[index].asset_id = QStringLiteral("timed-%1").arg(index);
    timed[index].capture_timestamp = QDateTime::fromString(times[index], Qt::ISODateWithMs);
    timed[index].capture_date_value = timed[index].capture_timestamp.date();
    timed[index].camera_label = QStringLiteral("Test camera");
  }
  model->replaceAssetsForTesting(timed);
  const auto first_timed = model->assetById(QStringLiteral("timed-0"));
  const auto later_timed = model->assetById(QStringLiteral("timed-1"));
  const auto earliest_timed = model->assetById(QStringLiteral("timed-2"));
  if (first_timed.value(QStringLiteral("burstSize")).toInt() != 2 ||
      earliest_timed.value(QStringLiteral("burstPosition")).toInt() != 1 ||
      later_timed.value(QStringLiteral("burstSize")).toInt() != 1 ||
      !first_timed.value(QStringLiteral("burstTimeBased")).toBool())
    return fail("Capture timestamps did not form independent short burst groups");
  model->setSortOrder(QStringLiteral("capture-asc"));
  if (model->assetAt(0).value(QStringLiteral("assetId")).toString() != QStringLiteral("timed-2"))
    return fail("Capture sorting did not use the original EXIF time");
  model->setSortOrder(QStringLiteral("capture-desc"));
  if (model->assetAt(0).value(QStringLiteral("assetId")).toString() != QStringLiteral("timed-1"))
    return fail("Newest capture sorting did not use the original EXIF time");
  model->setSortOrder(QStringLiteral("imported-desc"));
  model->replaceAssetsForTesting(items);
  const auto select_a = [&] {
    return model->selectAssetById(QStringLiteral("asset-a"));
  };
  const auto call_count = [&] {
    return read_file(root.filePath(QStringLiteral("metadata-calls"))).toInt();
  };
  const auto metadata_request = [&](int index) {
    return QJsonDocument::fromJson(read_file(root.filePath(QStringLiteral("metadata-request-%1.json").arg(index))))
        .object().value(QStringLiteral("records")).toArray();
  };
  const auto stored = [&](const QString& id) {
    return read_object(root.filePath(QStringLiteral("catalog-metadata.json")))
        .value(id).toObject().toVariantMap();
  };
  if (!select_a()) return fail("Could not select fixture photo");

  // Edits made while the catalog helper is still processing must update the
  // outbox immediately and survive acknowledgement of the older exact request.
  if (!write_file(root.filePath(QStringLiteral("hold-metadata")), "hold")) return 1;
  if (!model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("First revision")}}) ||
      !wait_until([&] { return call_count() == 1; })) return fail("Initial metadata revision did not dispatch");
  if (!model->saveMetadataToSelected({{QStringLiteral("caption"), QStringLiteral("Second revision")}}))
    return fail("Concurrent edit was rejected while catalog sync was pending");
  const auto first_pending = outbox_record(app_data, QStringLiteral("asset-a"));
  if (first_pending.value(QStringLiteral("title")).toString() != QStringLiteral("First revision") ||
      first_pending.value(QStringLiteral("caption")).toString() != QStringLiteral("Second revision") ||
      metadata_request(1).first().toObject().value(QStringLiteral("metadata")).toObject()
              .contains(QStringLiteral("caption"))) return fail("New revision was not separately checkpointed");
  if (!write_file(root.filePath(QStringLiteral("metadata-release-1")), "release") ||
      !wait_until([&] { return call_count() == 2; })) return fail("Latest metadata revision did not follow receipt");
  const auto second_request = metadata_request(2).first().toObject().value(QStringLiteral("metadata")).toObject();
  if (second_request.value(QStringLiteral("title")).toString() != QStringLiteral("First revision") ||
      second_request.value(QStringLiteral("caption")).toString() != QStringLiteral("Second revision"))
    return fail("Follow-up request did not carry the full newest record");
  if (!write_file(root.filePath(QStringLiteral("metadata-release-2")), "release") ||
      !wait_until([&] { return outbox_empty(app_data); })) return fail("Concurrent edit was acknowledged incorrectly");
  QFile::remove(root.filePath(QStringLiteral("hold-metadata")));
  if (stored(QStringLiteral("asset-a")) != first_pending) return fail("Catalog helper did not store the newest merged metadata");

  // A local checkpoint must be replayed after reopening the model, even when
  // the process that owned the first attempt was interrupted.
  if (!write_file(root.filePath(QStringLiteral("hold-metadata")), "hold") ||
      !model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Restarted edit")}}) ||
      !wait_until([&] { return call_count() == 3; })) return fail("Restart fixture did not hold its first attempt");
  const auto restart_record = outbox_record(app_data, QStringLiteral("asset-a"));
  model.reset();
  if (restart_record.value(QStringLiteral("title")).toString() != QStringLiteral("Restarted edit"))
    return fail("Shutdown lost locally durable metadata");
  QFile::remove(root.filePath(QStringLiteral("hold-metadata")));
  model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  if (!wait_until([&] { return call_count() == 4 && outbox_empty(app_data); }) ||
      stored(QStringLiteral("asset-a")).value(QStringLiteral("title")).toString() != QStringLiteral("Restarted edit"))
    return fail("Restart did not replay and clear the durable outbox");
  if (!select_a()) return fail("Could not reselect photo after restart");

  // Process errors and malformed receipts must retain local state until an
  // exact, complete receipt arrives on explicit retry.
  const auto next_call = [&] { return call_count() + 1; };
  if (!write_file(root.filePath(QStringLiteral("metadata-fail-%1").arg(next_call())), "fail") ||
      !model->saveMetadataToSelected({{QStringLiteral("caption"), QStringLiteral("Retry after failure")}}) ||
      !wait_until([&] { return call_count() == 5 && model->metadataStatus().contains(QStringLiteral("retained locally"), Qt::CaseInsensitive); }) ||
      outbox_record(app_data, QStringLiteral("asset-a")).value(QStringLiteral("caption")).toString() != QStringLiteral("Retry after failure"))
    return fail("Failed catalog write discarded its durable metadata");
  QFile::remove(root.filePath(QStringLiteral("metadata-fail-5")));
  model->retryMetadataPersistence();
  if (!wait_until([&] { return call_count() == 6 && outbox_empty(app_data); }) ||
      stored(QStringLiteral("asset-a")).value(QStringLiteral("caption")).toString() != QStringLiteral("Retry after failure"))
    return fail("Retry did not apply retained metadata");

  if (!write_file(root.filePath(QStringLiteral("metadata-bad-receipt-%1").arg(next_call())), "bad") ||
      !model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Retry after receipt")}}) ||
      !wait_until([&] { return call_count() == 7 && model->metadataStatus().contains(QStringLiteral("retained locally"), Qt::CaseInsensitive); }) ||
      outbox_record(app_data, QStringLiteral("asset-a")).value(QStringLiteral("title")).toString() != QStringLiteral("Retry after receipt"))
    return fail("Malformed receipt acknowledged metadata");
  QFile::remove(root.filePath(QStringLiteral("metadata-bad-receipt-7")));
  model->retryMetadataPersistence();
  if (!wait_until([&] { return call_count() == 8 && outbox_empty(app_data); }))
    return fail("Malformed-receipt recovery did not retry");

  // A partial missing record is retained without blocking the applied record.
  if (!select_a()) return fail("Could not select first photo for partial batch");
  model->selectAll();
  if (!write_file(root.filePath(QStringLiteral("metadata-missing-ids")), "asset-a\n") ||
      !model->saveMetadataToSelected({{QStringLiteral("keywords"), QStringList{QStringLiteral("trip"), QStringLiteral("selects")}}}) ||
      !wait_until([&] { return call_count() == 9 &&
          model->metadataStatus().contains(QStringLiteral("retained locally"), Qt::CaseInsensitive) &&
          outbox_record(app_data, QStringLiteral("asset-a")).contains(QStringLiteral("keywords")); }))
    return fail("Missing asset did not remain in the outbox");
  if (stored(QStringLiteral("asset-b")).value(QStringLiteral("keywords")).toList() !=
      QVariantList{QStringLiteral("trip"), QStringLiteral("selects")}) return fail("Partial receipt discarded or blocked its applied record");
  QFile::remove(root.filePath(QStringLiteral("metadata-missing-ids")));
  model->retryMetadataPersistence();
  if (!wait_until([&] { return call_count() == 10 && outbox_empty(app_data); }) ||
      stored(QStringLiteral("asset-a")).value(QStringLiteral("keywords")).toList() !=
          QVariantList{QStringLiteral("trip"), QStringLiteral("selects")}) return fail("Missing asset did not recover after retry");

  // C++ limits must reject payloads that the Rust catalog would not accept.
  if (!select_a()) return fail("Could not select photo for metadata bounds checks");
  const auto calls_before_invalid = call_count();
  if (model->saveMetadataToSelected({{QStringLiteral("unknown"), QStringLiteral("field")}}) ||
      model->saveMetadataToSelected({{QStringLiteral("title"), QString(16385, QLatin1Char('x'))}}) ||
      model->saveMetadataToSelected({{QStringLiteral("title"), QString(8193, QChar(0x00e9))}}) ||
      model->saveMetadataToSelected({{QStringLiteral("keywords"), QStringList{QString(513, QLatin1Char('k'))}}}) ||
      model->saveMetadataToSelected({{QStringLiteral("keywords"), QStringList{QString(257, QChar(0x00e9))}}}) ||
      model->saveMetadataToSelected({{QStringLiteral("keywords"), QStringList(257, QStringLiteral("tag"))}}) ||
      model->saveMetadataToSelected({{QStringLiteral("captureTime"), QStringLiteral("yesterday")}}) ||
      call_count() != calls_before_invalid) return fail("Out-of-contract metadata values were dispatched");

  // Two files with the same standard basename share one XMP sidecar. Writing
  // different records must stop before either file can silently overwrite the other.
  if (!select_a() ||
      !model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Frame A")}}) ||
      !wait_until([&] { return outbox_empty(app_data); })) return fail("Could not set first shared-basename record");
  if (!model->selectAssetById(QStringLiteral("asset-b")) ||
      !model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Frame B")}}) ||
      !wait_until([&] { return outbox_empty(app_data); })) return fail("Could not set second shared-basename record");
  model->selectAll();
  if (model->writeSelectedMetadataSidecars() ||
      !model->metadataStatus().contains(QStringLiteral("share a sidecar"), Qt::CaseInsensitive) ||
      QFileInfo::exists(metadata_sidecar_path(items.first().file_path)))
    return fail("Conflicting RAW/JPEG metadata was written through one sidecar");

  if (!model->saveMetadataToSelected({
          {QStringLiteral("title"), QStringLiteral("Shared title")},
          {QStringLiteral("caption"), QStringLiteral("Shared caption")},
          {QStringLiteral("creator"), QStringLiteral("Shared creator")},
          {QStringLiteral("copyright"), QStringLiteral("Shared copyright")},
          {QStringLiteral("location"), QStringLiteral("Shared location")},
          {QStringLiteral("captureTime"), QStringLiteral("2026-10-07T12:00:00Z")},
          {QStringLiteral("keywords"), QStringList{QStringLiteral("shared")}}}) ||
      !wait_until([&] { return outbox_empty(app_data); }) ||
      !model->writeSelectedMetadataSidecars() ||
      !wait_until([&] { return !model->metadataFileBusy(); })) return fail("Matching RAW/JPEG records did not write one sidecar");
  QString sidecar_error;
  const auto shared_path = metadata_sidecar_path(items.first().file_path);
  const auto shared_fields = read_metadata_sidecar(shared_path, &sidecar_error);
  if (!sidecar_error.isEmpty() || shared_fields.value(QStringLiteral("title")).toString() != QStringLiteral("Shared title"))
    return fail("Shared-basename sidecar did not contain the common metadata");

  // Hold the async sidecar worker so the user's immediate edit attempt is
  // deterministic, then let a newer catalog snapshot land before that worker.
  // Sidecar fields should overlay that fresh snapshot only; unrelated fields
  // must not be replaced by the worker's stale capture of asset_metadata_.
  if (!model->selectAssetById(QStringLiteral("asset-a")) ||
      !model->saveMetadataToSelected({
          {QStringLiteral("title"), QStringLiteral("Old catalog title")},
          {QStringLiteral("caption"), QStringLiteral("Old catalog caption")}}) ||
      !wait_until([&] { return outbox_empty(app_data); })) return fail("Could not prepare stale-snapshot sidecar fixture");
  const auto sidecar_content = QByteArray("<?xml version=\"1.0\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Sidecar title</rdf:li></rdf:Alt></dc:title></rdf:Description></rdf:RDF></x:xmpmeta>");
  if (!write_file(shared_path, sidecar_content) ||
      !write_snapshot(root_path,
          {{QStringLiteral("title"), QStringLiteral("Fresh catalog title")},
           {QStringLiteral("caption"), QStringLiteral("Fresh catalog caption")},
           {QStringLiteral("keywords"), QStringList{QStringLiteral("catalog")}}},
          {{QStringLiteral("title"), QStringLiteral("Frame B")}})) return fail("Could not update snapshot fixture");
  const int sidecar_sync_call = call_count() + 1;
  GlobalPoolGate pool_gate;
  if (!pool_gate.ready || !model->readSelectedMetadataSidecars() || !model->metadataFileBusy())
    return fail("Could not hold sidecar import in flight");
  if (model->saveMetadataToSelected({{QStringLiteral("caption"), QStringLiteral("Must wait for XMP read")}}))
    return fail("Catalog metadata mutation should wait while a sidecar read is pending");
  model->refresh();
  if (!wait_until([&] { return !model->loading(); }) || !model->metadataFileBusy())
    return fail("Newer snapshot did not complete while sidecar import remained pending");
  if (model->selectedMetadata().value(QStringLiteral("caption")).toString() != QStringLiteral("Fresh catalog caption"))
    return fail("Catalog snapshot fixture did not publish fresh metadata");

  if (!write_file(root.filePath(QStringLiteral("hold-metadata")), "hold")) return 1;
  pool_gate.release.release();
  if (!wait_until([&] { return call_count() == sidecar_sync_call; })) return fail("Sidecar import was not journaled after releasing its worker");
  const auto merged_after_read = model->selectedMetadata();
  if (merged_after_read.value(QStringLiteral("title")).toString() != QStringLiteral("Sidecar title") ||
      merged_after_read.value(QStringLiteral("caption")).toString() != QStringLiteral("Fresh catalog caption") ||
      merged_after_read.value(QStringLiteral("keywords")).toList() != QVariantList{QStringLiteral("catalog")})
    return fail("Async sidecar completion replaced fresh catalog fields with its stale captured record");
  const auto read_pending = outbox_record(app_data, QStringLiteral("asset-a"));
  if (read_pending != merged_after_read) return fail("Imported XMP fields were not locally durable before acknowledgement");
  if (!write_file(root.filePath(QStringLiteral("metadata-release-%1").arg(sidecar_sync_call)), "release") ||
      !wait_until([&] { return outbox_empty(app_data); })) return fail("Imported XMP record did not sync after receipt");
  QFile::remove(root.filePath(QStringLiteral("hold-metadata")));

  // If the local atomic checkpoint fails after XMP parsing, the user must see
  // the write failure instead of a success message that promises persistence.
  const auto preserved_outbox_path = outbox_path + QStringLiteral(".preserved");
  const auto empty_outbox_bytes = read_file(outbox_path);
  if (!QFile::rename(outbox_path, preserved_outbox_path) || !QDir().mkdir(outbox_path))
    return fail("Could not prepare a metadata checkpoint failure");
  const QByteArray unsaved_sidecar("<?xml version=\"1.0\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Must not claim saved</rdf:li></rdf:Alt></dc:title></rdf:Description></rdf:RDF></x:xmpmeta>");
  if (!write_file(shared_path, unsaved_sidecar) || !model->readSelectedMetadataSidecars() ||
      !wait_until([&] { return !model->metadataFileBusy(); })) return fail("Could not exercise failed XMP checkpoint");
  if (!model->metadataStatus().contains(QStringLiteral("could not be preserved locally"), Qt::CaseInsensitive) ||
      model->metadataStatus().contains(QStringLiteral("saved locally"), Qt::CaseInsensitive) ||
      model->selectedMetadata().value(QStringLiteral("title")).toString() != QStringLiteral("Sidecar title") ||
      call_count() != sidecar_sync_call)
    return fail("Failed XMP checkpoint was reported as saved or applied in memory");
  if (!QDir().rmdir(outbox_path) || !QFile::rename(preserved_outbox_path, outbox_path) ||
      read_file(outbox_path) != empty_outbox_bytes) return fail("Could not restore the metadata fixture outbox");

  // Missing records in the first 256-item batch must not starve later valid
  // edits. They remain recoverable while the next batch continues syncing.
  QVector<CatalogAssetItem> large_batch;
  for (int index = 0; index < 257; ++index) {
    auto item = items.first();
    item.asset_id = QStringLiteral("large-%1").arg(index, 3, 10, QLatin1Char('0'));
    large_batch.append(item);
  }
  model->replaceAssetsForTesting(large_batch);
  model->selectAll();
  const auto calls_before_large = call_count();
  if (!write_file(root.filePath(QStringLiteral("metadata-missing-ids")), "large-000\n") ||
      !model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Large batch")}}) ||
      !wait_until([&] { return call_count() == calls_before_large + 2 && model->metadataPendingCount() == 1; }) ||
      stored(QStringLiteral("large-256")).value(QStringLiteral("title")).toString() != QStringLiteral("Large batch"))
    return fail("A missing first-batch record starved later valid metadata edits");
  QFile::remove(root.filePath(QStringLiteral("metadata-missing-ids")));
  model->retryMetadataPersistence();
  if (!wait_until([&] { return call_count() == calls_before_large + 3 && outbox_empty(app_data); }))
    return fail("Large-batch missing metadata did not recover on retry");
  const auto final_sync_call = call_count();
  model.reset();
  const QByteArray corrupt_outbox("{unreadable metadata recovery");
  if (!write_file(outbox_path, corrupt_outbox)) return fail("Could not prepare malformed metadata outbox");
  model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  model->selectAssetById(QStringLiteral("asset-a"));
  if (model->saveMetadataToSelected({{QStringLiteral("title"), QStringLiteral("Do not replace recovery")}}))
    return fail("Unreadable local recovery should block a metadata mutation");
  model.reset();
  if (read_file(outbox_path) != corrupt_outbox || call_count() != final_sync_call)
    return fail("Malformed metadata recovery was replaced or dispatched");

  std::cout << "Metadata persistence contract passed: exact local outbox revisions, async edit merge, restart replay, failure/retry, malformed receipt, partial missing, C++ limits, shared RAW/JPEG basename conflict, sidecar busy guard, fresh-snapshot merge, malformed recovery preservation.\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_METADATA_FIXTURE_ROOT") && app.arguments().size() > 1)
    return helper(app.arguments());

  QCoreApplication::setOrganizationName("PhotoEditorTests");
  QCoreApplication::setApplicationName("CatalogMetadataPersistenceTest");
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Could not create metadata fixture root");
  const QDir root(temporary.path());
  const QDir binaries(root.filePath(QStringLiteral("rust/target/debug")));
  if (!QDir().mkpath(binaries.absolutePath()) ||
      !write_file(root.filePath(QStringLiteral("rust/Cargo.toml")), QByteArray("# fixture\n"))) {
    return fail("Could not initialize helper fixture directories");
  }
  for (const auto& name : {QStringLiteral("catalog_state"), QStringLiteral("catalog_snapshot")}) {
    if (!QFile::link(app.applicationFilePath(), binaries.filePath(name))) return fail("Could not link fixture helper binary");
  }
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.absolutePath().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath(QStringLiteral("data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://fixture.invalid/unused");
  qputenv("PHOTO_EDITOR_METADATA_FIXTURE_ROOT", root.absolutePath().toUtf8());
  if (AppBootstrap::helperProgram(QStringLiteral("catalog_state")) != binaries.filePath(QStringLiteral("catalog_state")))
    return fail("Unsafe catalog helper resolution");
  return run_contracts(root.absolutePath());
}
