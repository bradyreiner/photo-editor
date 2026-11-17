#include "photo_component_asset_storage.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>

using namespace photo_editor::desktop;
namespace storage = photo_editor::desktop::component_asset_storage;
namespace table = photo_editor::desktop::component_object_table;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

QString uuid(int suffix) {
  return QStringLiteral("10000000-0000-4000-8000-%1")
      .arg(suffix, 12, 10, QLatin1Char('0'));
}

QString sha256(char fill) {
  return QString(64, QLatin1Char(fill));
}

QVariantMap descriptor(const QString& content_id, const QString& digest,
                       const QString& file_name = QStringLiteral("asset.png")) {
  return {{QStringLiteral("schema"), QStringLiteral("looksy.smart-object.source")},
          {QStringLiteral("version"), 1},
          {QStringLiteral("contentId"), content_id},
          {QStringLiteral("policy"), QStringLiteral("embedded")},
          {QStringLiteral("payloadKind"), QStringLiteral("layer-png")},
          {QStringLiteral("sourceSha256"), digest},
          {QStringLiteral("byteLength"), 8},
          {QStringLiteral("fileName"), file_name},
          {QStringLiteral("intrinsicWidth"), 2},
          {QStringLiteral("intrinsicHeight"), 1},
          {QStringLiteral("dataBase64"), QStringLiteral("AAECAP8BAgM=")}};
}

QVariantMap document(const QString& content_id, int revision,
                     const QByteArray& pixels,
                     const QVariantList& layers = {},
                     const QByteArray& authored_attachment = {}) {
  QVariantList document_layers = layers;
  if (document_layers.isEmpty()) {
    document_layers.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("source-pixels")},
        {QStringLiteral("kind"), QStringLiteral("pixel")},
        {QStringLiteral("pixelWidth"), 2},
        {QStringLiteral("pixelHeight"), 1},
        {QStringLiteral("pixelData"), pixels},
        {QStringLiteral("authoredBinaryAttachment"), authored_attachment},
        {QStringLiteral("authoredPixelField"), QStringLiteral("retain")}});
  }
  return {{QStringLiteral("schema"), QStringLiteral("looksy.smart-object.document")},
          {QStringLiteral("version"), 1},
          {QStringLiteral("contentId"), content_id},
          {QStringLiteral("revision"), revision},
          {QStringLiteral("canvasWidth"), 2},
          {QStringLiteral("canvasHeight"), 1},
          {QStringLiteral("layers"), document_layers},
          {QStringLiteral("maskStack"), QVariantList{QVariantMap{
              {QStringLiteral("id"), QStringLiteral("inner-mask")},
              {QStringLiteral("kind"), QStringLiteral("raster")},
              {QStringLiteral("maskData"), QByteArray::fromHex("00ff")}}}},
          {QStringLiteral("authoredDocumentField"), QStringLiteral("preserve")}};
}

QVariantMap instance(const QString& layer_id, const QVariantMap& content,
                     const QVariantMap& nested_document) {
  return {{QStringLiteral("id"), layer_id},
          {QStringLiteral("kind"), QStringLiteral("smart-object")},
          {QStringLiteral("smartObject"), content},
          {QStringLiteral("smartObjectInstanceId"), QStringLiteral("instance-") + layer_id},
          {QStringLiteral("smartObjectDocument"), nested_document},
          {QStringLiteral("maskIds"), QVariantList{QStringLiteral("outer-mask")}},
          {QStringLiteral("maskData"), QByteArray::fromHex("11223344")},
          {QStringLiteral("transform"), QVariantMap{
              {QStringLiteral("x"), 33},
              {QStringLiteral("y"), -7},
              {QStringLiteral("rotation"), 12.5}}},
          {QStringLiteral("opacity"), 0.63},
          {QStringLiteral("blendMode"), QStringLiteral("screen")},
          {QStringLiteral("style"), QVariantMap{
              {QStringLiteral("stroke"), true},
              {QStringLiteral("color"), QStringLiteral("#3a7fc1")}}},
          {QStringLiteral("authoredLayerField"), QStringLiteral("keep")}};
}

struct DiskStats {
  qsizetype file_count = 0;
  qint64 total_bytes = 0;
};

DiskStats disk_stats(const QString& root) {
  DiskStats result;
  QDirIterator iterator(root, QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QFileInfo info(iterator.next());
    ++result.file_count;
    result.total_bytes += info.size();
  }
  return result;
}

struct StoredRun {
  DiskStats disk;
  qint64 wrapper_bytes = 0;
  qint64 shared_row_bytes = 0;
  QSet<QString> shared_row_asset_ids;
  QVariantList runtime_layers;
};

StoredRun store_shared_instances(qsizetype count) {
  StoredRun result;
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary asset root is available");
  if (!temporary.isValid()) return result;
  const RecipeAssetStore asset_store(temporary.path());
  const auto content = descriptor(uuid(10), sha256('a'));
  const QByteArray source_bytes("\0\x01\x02\xff\x10\x80\0\x7f", 8);
  constexpr qsizetype authored_attachment_bytes = 1024 * 1024;
  const QByteArray authored_attachment(authored_attachment_bytes, char(0x5a));
  const auto nested_document = document(
      uuid(11), 3, source_bytes, {}, authored_attachment);
  result.runtime_layers.reserve(count);
  for (qsizetype index = 0; index < count; ++index) {
    result.runtime_layers.append(instance(
        QStringLiteral("instance-layer-%1").arg(index),
        content, nested_document));
  }

  QVariantList prepared;
  int object_asset_count = 0;
  QString error;
  const auto prepared_ok = storage::prepare_component_layer_asset_wrappers(
      result.runtime_layers, asset_store, &prepared,
      &object_asset_count, &error);
  check(prepared_ok, "shared component object rows are prepared and stored");
  if (!prepared_ok) return result;
  check(object_asset_count == 2,
        "one descriptor and one nested document asset are stored for all instances");

  QSet<QString> wrapper_ids;
  QSet<QString> row_ids;
  for (qsizetype index = 0; index < prepared.size(); ++index) {
    const auto wrapper_asset = asset_store.putVariant(prepared.at(index));
    check(wrapper_asset.ok(), "component layer wrapper asset is stored");
    if (!wrapper_asset.ok()) continue;
    const auto wrapper_id = QString::fromStdString(wrapper_asset.asset->value);
    wrapper_ids.insert(wrapper_id);
    const auto wrapper_bytes = asset_store.get(*wrapper_asset.asset, &error);
    if (wrapper_bytes.has_value()) result.wrapper_bytes += wrapper_bytes->size();

    const auto prepared_map = prepared.at(index).toMap();
    const auto wrapper = prepared_map.value(QString::fromLatin1(
        storage::kWrapperField)).toMap();
    const auto object_assets = wrapper.value(QStringLiteral("objectAssets")).toList();
    check(object_assets.size() == 2,
          "each placement wrapper carries only its two-row transitive closure");
    for (const auto& binding_value : object_assets) {
      const auto binding = binding_value.toMap();
      row_ids.insert(binding.value(QStringLiteral("assetId")).toString());
    }

    QString restore_error;
    const auto stored_wrapper = asset_store.getVariant(*wrapper_asset.asset,
                                                       &restore_error);
    check(stored_wrapper.has_value() && stored_wrapper->canConvert<QVariantMap>(),
          "stored component wrapper decodes");
    if (!stored_wrapper.has_value() || !stored_wrapper->canConvert<QVariantMap>())
      continue;
    QVariantMap restored_layer;
    const auto restored = storage::restore_component_layer_asset_wrapper(
        stored_wrapper->toMap(), asset_store, &restored_layer, &restore_error);
    check(restored, "component wrapper restores its layer and shared closure");
    if (!restored) continue;
    check(restored_layer.value(QStringLiteral("id")) ==
              result.runtime_layers.at(index).toMap().value(QStringLiteral("id")) &&
              restored_layer.value(QStringLiteral("transform")) ==
                  result.runtime_layers.at(index).toMap().value(QStringLiteral("transform")) &&
              restored_layer.value(QStringLiteral("maskIds")) ==
                  result.runtime_layers.at(index).toMap().value(QStringLiteral("maskIds")) &&
              restored_layer.value(QStringLiteral("style")) ==
                  result.runtime_layers.at(index).toMap().value(QStringLiteral("style")),
          "placement transform, masks, style, and authored fields are retained");
    const auto restored_document = restored_layer
        .value(QStringLiteral("smartObjectDocument")).toMap();
    const auto pixel_layer = restored_document
        .value(QStringLiteral("layers")).toList().constFirst().toMap();
    check(pixel_layer.value(QStringLiteral("pixelData")).toByteArray() ==
              source_bytes,
          "embedded document bytes round-trip exactly through row assets");
    check(pixel_layer.value(QStringLiteral("authoredBinaryAttachment")).toByteArray() ==
              authored_attachment,
          "a 1 MiB authored binary field round-trips exactly through shared row assets");
    check(restored_document.value(QStringLiteral("maskStack")).toList()
              .constFirst().toMap().value(QStringLiteral("maskData")).toByteArray() ==
              QByteArray::fromHex("00ff"),
          "nested document mask bytes round-trip exactly");
  }

  check(wrapper_ids.size() == count,
        "each independently placed layer gets a small placement wrapper asset");
  check(row_ids.size() == 2,
        "all placements point to the same two physical object assets");
  result.shared_row_asset_ids = row_ids;
  for (const auto& asset_id : row_ids) {
    QString error;
    const auto bytes = asset_store.get(
        photo_editor::pipeline::AssetIdentifier{asset_id.toStdString()}, &error);
    if (bytes.has_value()) result.shared_row_bytes += bytes->size();
    else check(false, "shared row asset remains available");
  }
  result.disk = disk_stats(temporary.path());
  check(result.disk.file_count == count + 2,
        "physical asset count is placements plus unique shared object rows");
  check(result.disk.total_bytes ==
            result.wrapper_bytes + result.shared_row_bytes,
        "total physical bytes equal placement wrappers plus unique row assets");
  return result;
}

void shared_body_storage_scales_by_identity() {
  const auto one = store_shared_instances(1);
  const auto three = store_shared_instances(3);
  const auto thirty = store_shared_instances(30);
  check(one.disk.file_count == 3 && three.disk.file_count == 5 &&
            thirty.disk.file_count == 32,
        "1/3/30 instances each store one descriptor and one document body");
  check(one.shared_row_asset_ids == three.shared_row_asset_ids &&
            one.shared_row_asset_ids == thirty.shared_row_asset_ids,
        "shared object asset IDs stay constant as instance count grows");
  check(one.shared_row_bytes == three.shared_row_bytes &&
            one.shared_row_bytes == thirty.shared_row_bytes,
        "shared object disk bytes do not grow with placement count");
  check(one.shared_row_bytes > 1024 * 1024,
        "the physical shared rows include the full 1 MiB authored binary payload");
  check(thirty.disk.total_bytes - one.disk.total_bytes ==
            thirty.wrapper_bytes - one.wrapper_bytes,
        "total byte growth from one to thirty is limited to placement wrappers");
  const QJsonObject receipt{
      {QStringLiteral("measurement"), QStringLiteral("component-asset-storage-growth")},
      {QStringLiteral("authoredBinaryPayloadBytes"), 1024 * 1024},
      {QStringLiteral("instances"), QJsonObject{
          {QStringLiteral("1"), QJsonObject{
              {QStringLiteral("files"), static_cast<qint64>(one.disk.file_count)},
              {QStringLiteral("totalBytes"), one.disk.total_bytes},
              {QStringLiteral("sharedRowBytes"), one.shared_row_bytes},
              {QStringLiteral("wrapperBytes"), one.wrapper_bytes}}},
          {QStringLiteral("3"), QJsonObject{
              {QStringLiteral("files"), static_cast<qint64>(three.disk.file_count)},
              {QStringLiteral("totalBytes"), three.disk.total_bytes},
              {QStringLiteral("sharedRowBytes"), three.shared_row_bytes},
              {QStringLiteral("wrapperBytes"), three.wrapper_bytes}}},
          {QStringLiteral("30"), QJsonObject{
              {QStringLiteral("files"), static_cast<qint64>(thirty.disk.file_count)},
              {QStringLiteral("totalBytes"), thirty.disk.total_bytes},
              {QStringLiteral("sharedRowBytes"), thirty.shared_row_bytes},
              {QStringLiteral("wrapperBytes"), thirty.wrapper_bytes}}}}}};
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Compact);
  std::cout << "storage_receipt=" << receipt_bytes.constData() << '\n';
}

void nested_and_independent_closures() {
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary closure asset root is available");
  if (!temporary.isValid()) return;
  const RecipeAssetStore asset_store(temporary.path());
  const auto content = descriptor(uuid(20), sha256('b'));
  const auto inner = document(uuid(21), 1, QByteArray::fromHex("0908070605040302"));
  const QVariantList outer_layers{
      instance(QStringLiteral("child-1"), content, inner),
      instance(QStringLiteral("child-2"), content, inner)};
  const auto outer = document(uuid(22), 4, QByteArray{}, outer_layers);
  QVariantList nested_state{instance(QStringLiteral("outer"), content, outer)};
  QVariantList prepared_nested;
  int nested_row_count = 0;
  QString error;
  const auto nested_ok = storage::prepare_component_layer_asset_wrappers(
      nested_state, asset_store, &prepared_nested, &nested_row_count, &error);
  check(nested_ok && nested_row_count == 3,
        "nested shared closure stores one descriptor and two document rows");
  if (nested_ok) {
    const auto wrapper = prepared_nested.constFirst().toMap()
        .value(QString::fromLatin1(storage::kWrapperField)).toMap();
    const auto closure = wrapper.value(QStringLiteral("objectAssets")).toList();
    check(closure.size() == 3,
          "outer placement wrapper carries its full nested reference closure");
    const auto stored_wrapper = asset_store.putVariant(prepared_nested.constFirst());
    check(stored_wrapper.ok(),
          "outer nested component wrapper asset is stored");
    if (stored_wrapper.ok()) {
      const auto fetched = asset_store.getVariant(*stored_wrapper.asset, &error);
      check(fetched.has_value() && fetched->canConvert<QVariantMap>(),
            "outer nested component wrapper asset decodes");
      if (fetched.has_value() && fetched->canConvert<QVariantMap>()) {
        QVariantMap restored;
        const auto restored_ok = storage::restore_component_layer_asset_wrapper(
            fetched->toMap(), asset_store, &restored, &error);
        check(restored_ok &&
                  restored.value(QStringLiteral("smartObjectDocument")).toMap()
                      .value(QStringLiteral("contentId")).toString() == uuid(22),
              "nested shared document closure restores its authored tree");
      }
    }
  }

  const auto first_content = descriptor(uuid(30), sha256('c'));
  const auto second_content = descriptor(uuid(31), sha256('d'));
  const auto first_document = document(uuid(32), 1, QByteArray::fromHex("aabbccdd"));
  const auto second_document = document(uuid(33), 1, QByteArray::fromHex("11223344"));
  const QVariantList independent_layers{
      instance(QStringLiteral("independent-a"), first_content, first_document),
      instance(QStringLiteral("independent-b"), second_content, second_document)};
  QVariantList prepared_independent;
  int independent_row_count = 0;
  const auto independent_ok = storage::prepare_component_layer_asset_wrappers(
      independent_layers, asset_store, &prepared_independent,
      &independent_row_count, &error);
  check(independent_ok && independent_row_count == 4,
        "different component identities remain distinct object assets");
  if (independent_ok) {
    const auto first_refs = prepared_independent.at(0).toMap()
        .value(QString::fromLatin1(storage::kWrapperField)).toMap()
        .value(QStringLiteral("objectAssets")).toList();
    const auto second_refs = prepared_independent.at(1).toMap()
        .value(QString::fromLatin1(storage::kWrapperField)).toMap()
        .value(QStringLiteral("objectAssets")).toList();
    check(first_refs.size() == 2 && second_refs.size() == 2,
          "each independent placement has only its own transitive closure");
    QSet<QString> first_ids;
    QSet<QString> second_ids;
    for (const auto& value : first_refs)
      first_ids.insert(value.toMap().value(QStringLiteral("assetId")).toString());
    for (const auto& value : second_refs)
      second_ids.insert(value.toMap().value(QStringLiteral("assetId")).toString());
    for (const auto& id : first_ids)
      check(!second_ids.contains(id),
            "independent content does not share object row assets");
  }
}

void malformed_wrappers_fail_safely() {
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary malformed asset root is available");
  if (!temporary.isValid()) return;
  const RecipeAssetStore asset_store(temporary.path());
  const auto layer = instance(QStringLiteral("tamper-target"),
      descriptor(uuid(40), sha256('e')),
      document(uuid(41), 1, QByteArray::fromHex("01020304")));
  QVariantList prepared;
  int stored_count = 0;
  QString error;
  const auto prepared_ok = storage::prepare_component_layer_asset_wrappers(
      QVariantList{layer}, asset_store, &prepared, &stored_count, &error);
  check(prepared_ok && stored_count == 2,
        "valid component layer prepares for malformed wrapper checks");
  if (!prepared_ok) return;
  auto stored_layer = prepared.constFirst().toMap();
  const auto original_wrapper = stored_layer.value(QString::fromLatin1(
      storage::kWrapperField)).toMap();

  const auto restore = [&](const QVariantMap& candidate, QVariantMap* output) {
    QString local_error;
    return storage::restore_component_layer_asset_wrapper(
        candidate, asset_store, output, &local_error);
  };
  QVariantMap output;
  auto unknown_version = stored_layer;
  auto wrapper = original_wrapper;
  wrapper.insert(QStringLiteral("version"), 2);
  unknown_version.insert(QString::fromLatin1(storage::kWrapperField), wrapper);
  const auto unknown_before = unknown_version;
  check(!restore(unknown_version, &output) && unknown_version == unknown_before,
        "unsupported wrapper versions fail without mutating input");

  auto missing_asset = stored_layer;
  wrapper = original_wrapper;
  auto assets = wrapper.value(QStringLiteral("objectAssets")).toList();
  auto first_binding = assets.first().toMap();
  first_binding.insert(QStringLiteral("assetId"),
                       QStringLiteral("sha256:") + QString(64, QLatin1Char('f')));
  assets[0] = first_binding;
  wrapper.insert(QStringLiteral("objectAssets"), assets);
  missing_asset.insert(QString::fromLatin1(storage::kWrapperField), wrapper);
  check(!restore(missing_asset, &output),
        "missing object row assets are rejected");

  auto tampered_metadata = stored_layer;
  wrapper = original_wrapper;
  assets = wrapper.value(QStringLiteral("objectAssets")).toList();
  first_binding = assets.first().toMap();
  first_binding.insert(QStringLiteral("contentId"), uuid(99));
  assets[0] = first_binding;
  wrapper.insert(QStringLiteral("objectAssets"), assets);
  tampered_metadata.insert(QString::fromLatin1(storage::kWrapperField), wrapper);
  check(!restore(tampered_metadata, &output),
        "tampered row identity metadata is rejected");

  auto dangling = stored_layer;
  wrapper = original_wrapper;
  assets = wrapper.value(QStringLiteral("objectAssets")).toList();
  assets.removeFirst();
  wrapper.insert(QStringLiteral("objectAssets"), assets);
  dangling.insert(QString::fromLatin1(storage::kWrapperField), wrapper);
  check(!restore(dangling, &output),
        "incomplete transitive object closures are rejected");

  check(!storage::has_component_layer_asset_wrapper(
            QVariantMap{{QStringLiteral("id"), QStringLiteral("legacy")}}),
        "legacy plain layer assets remain distinguishable from wrappers");
}

}  // namespace

int main() {
  shared_body_storage_scales_by_identity();
  nested_and_independent_closures();
  malformed_wrappers_fail_safely();
  if (failures != 0) {
    std::cerr << failures << " component asset storage checks failed\n";
    return 1;
  }
  std::cout << "component asset storage checks passed\n";
  return 0;
}
