#include "photo_component_object_table.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>

using namespace photo_editor::desktop;
namespace table = photo_editor::desktop::component_object_table;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

QString uuid(int suffix) {
  return QStringLiteral("00000000-0000-4000-8000-%1")
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
          {QStringLiteral("byteLength"), 4},
          {QStringLiteral("fileName"), file_name},
          {QStringLiteral("intrinsicWidth"), 1},
          {QStringLiteral("intrinsicHeight"), 1},
          {QStringLiteral("dataBase64"), QStringLiteral("AQIDBA==")}};
}

QVariantMap raw_document(const QString& content_id, int revision,
                         const QVariantList& layers,
                         const QString& authored_note = QStringLiteral("kept")) {
  return {{QStringLiteral("schema"), QStringLiteral("looksy.smart-object.document")},
          {QStringLiteral("version"), 1},
          {QStringLiteral("contentId"), content_id},
          {QStringLiteral("revision"), revision},
          {QStringLiteral("canvasWidth"), 8},
          {QStringLiteral("canvasHeight"), 6},
          {QStringLiteral("layers"), layers},
          {QStringLiteral("maskStack"), QVariantList{}},
          {QStringLiteral("authoredNote"), authored_note}};
}

QVariantMap json_document(QVariantMap document) {
  return encode_component_payloads(document).toMap();
}

QVariantMap instance_layer(const QString& layer_id,
                          const QVariantMap& content,
                          const QVariantMap& document) {
  return {{QStringLiteral("id"), layer_id},
          {QStringLiteral("kind"), QStringLiteral("smart-object")},
          {QStringLiteral("smartObject"), content},
          {QStringLiteral("smartObjectInstanceId"), QStringLiteral("instance-") + layer_id},
          {QStringLiteral("smartObjectDocument"), document},
          {QStringLiteral("visible"), true},
          {QStringLiteral("blendMode"), QStringLiteral("multiply")},
          {QStringLiteral("opacity"), 0.73},
          {QStringLiteral("transform"), QVariantMap{
               {QStringLiteral("x"), 19.25}, {QStringLiteral("y"), -8.5},
               {QStringLiteral("rotation"), 13.0}}},
          {QStringLiteral("maskIds"), QVariantList{QStringLiteral("mask-a")}},
          {QStringLiteral("style"), QVariantMap{
               {QStringLiteral("dropShadow"), true},
               {QStringLiteral("color"), QStringLiteral("#804020")}}},
          {QStringLiteral("authoredLayerField"), QStringLiteral("retain")}};
}

QVariantMap basic_document() {
  QVariantMap pixel_layer{
      {QStringLiteral("id"), QStringLiteral("pixels")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), 1},
      {QStringLiteral("pixelHeight"), 1},
      {QStringLiteral("pixelData"), QByteArray::fromHex("01020304")},
      {QStringLiteral("transform"), QVariantMap{
          {QStringLiteral("x"), 2}, {QStringLiteral("y"), 3}}}};
  return json_document(raw_document(uuid(20), 2, {pixel_layer}));
}

QVariantMap make_recipe(qsizetype count, const QVariantMap& content,
                        const QVariantMap& document) {
  QVariantList layers;
  layers.reserve(count);
  for (qsizetype i = 0; i < count; ++i) {
    const auto id = QStringLiteral("layer-%1").arg(i);
    layers.append(instance_layer(id, content, document));
  }
  return {{QStringLiteral("recipeVersion"), 18},
          {QStringLiteral("layers"), layers},
          {QStringLiteral("photoLayers"), layers},
          {QStringLiteral("unknownRootField"), QVariantMap{
              {QStringLiteral("kept"), QStringLiteral("yes")}}}};
}

QVariantMap decoded_recipe(QVariantMap recipe) {
  for (const auto* key : {"layers", "photoLayers"}) {
    QVariantList layers = recipe.value(QString::fromLatin1(key)).toList();
    for (auto& value : layers) {
      auto layer = value.toMap();
      if (layer.contains(QStringLiteral("smartObjectDocument"))) {
        layer.insert(QStringLiteral("smartObjectDocument"),
                     decode_component_payloads(
                         layer.value(QStringLiteral("smartObjectDocument"))));
      }
      value = layer;
    }
    recipe.insert(QString::fromLatin1(key), layers);
  }
  return recipe;
}

qsizetype count_type(const QVariantList& objects, const QString& type) {
  qsizetype count = 0;
  for (const auto& value : objects)
    if (value.toMap().value(QStringLiteral("type")).toString() == type)
      ++count;
  return count;
}

void shared_instance_counts_round_trip() {
  const auto content = descriptor(uuid(10), sha256('a'));
  const auto document = basic_document();
  QVariantList first_reference_rows;
  for (const auto count : {qsizetype{1}, qsizetype{3}, qsizetype{30}}) {
    const auto original = make_recipe(count, content, document);
    auto stored = original;
    QString error;
    const auto externalized = table::externalize_component_objects(&stored, &error);
    check(externalized, "shared objects externalize");
    if (!externalized) continue;
    if (stored.contains(QString::fromLatin1(table::kTableField))) {
      const auto objects = stored.value(QString::fromLatin1(table::kTableField))
                               .toMap().value(QStringLiteral("objects")).toList();
      QVariantList current_refs;
      for (const auto& object : objects)
        current_refs.append(object.toMap().value(QStringLiteral("ref")));
      if (first_reference_rows.isEmpty()) first_reference_rows = current_refs;
      check(objects.size() == 2,
            "one descriptor and one document row are stored at every multiplicity");
      check(count_type(objects, QString::fromLatin1(table::kDescriptorType)) == 1,
            "descriptor identity is stored once");
      check(count_type(objects, QString::fromLatin1(table::kDocumentType)) == 1,
            "document identity is stored once");
      check(current_refs == first_reference_rows,
            "table references stay deterministic as instance count changes");
    }
    check(table::has_table(stored), "stored recipe exposes the table field");
    check(table::has_references(stored), "stored recipe exposes versioned references");
    const auto first_stored_layer = stored.value(QStringLiteral("layers"))
                                        .toList().constFirst().toMap();
    check(!first_stored_layer.contains(QStringLiteral("smartObject")) &&
              !first_stored_layer.contains(QStringLiteral("smartObjectDocument")) &&
              first_stored_layer.contains(QString::fromLatin1(
                  table::kDescriptorReferenceField)) &&
              first_stored_layer.contains(QString::fromLatin1(
                  table::kDocumentReferenceField)),
          "stored layer retains only refs for component payloads");

    const auto inlined = table::inline_component_objects(&stored, &error);
    check(inlined, "shared objects inline");
    if (!inlined) continue;
    check(!table::has_table(stored) && !table::has_references(stored),
          "inline recipe has no storage table or refs");
    check(stored == original,
          "JSON-ready runtime layer maps round-trip exactly");
    auto decoded = decoded_recipe(stored);
    const auto decoded_layers = decoded.value(QStringLiteral("layers")).toList();
    const auto original_layers = decoded_recipe(original)
                                     .value(QStringLiteral("layers")).toList();
    check(decoded_layers == original_layers,
          "tagged byte payload restores through the existing decode boundary");
  }
}

void recipe_json_growth_receipt() {
  const auto content = descriptor(uuid(10), sha256('a'));
  auto document = basic_document();
  document.insert(QStringLiteral("authoredBody"), QString(64 * 1024, QLatin1Char('x')));
  qsizetype body_bytes = -1;
  for (const auto count : {qsizetype{1}, qsizetype{3}, qsizetype{30}}) {
    const auto original = make_recipe(count, content, document);
    auto stored = original;
    QString error;
    check(table::externalize_component_objects(&stored, &error), "JSON size fixture externalizes");
    const auto inline_bytes = QJsonDocument(QJsonObject::fromVariantMap(original)).toJson(QJsonDocument::Compact).size();
    const auto packed_bytes = QJsonDocument(QJsonObject::fromVariantMap(stored)).toJson(QJsonDocument::Compact).size();
    const auto current_body = QJsonDocument(QJsonObject::fromVariantMap(
        stored.value(QString::fromLatin1(table::kTableField)).toMap())).toJson(QJsonDocument::Compact).size();
    if (body_bytes < 0) body_bytes = current_body;
    check(body_bytes == current_body, "serialized shared JSON body is constant at 1/3/30 placements");
    if (count == 30) check(packed_bytes < inline_bytes / 4, "thirty-instance JSON avoids repeated full bodies");
    std::cout << QJsonDocument(QJsonObject{{"storage", "json-recipe"}, {"instances", static_cast<int>(count)},
        {"inlineBytes", static_cast<double>(inline_bytes)}, {"packedBytes", static_cast<double>(packed_bytes)},
        {"sharedBodyBytes", static_cast<double>(current_body)}}).toJson(QJsonDocument::Compact).constData() << '\n';
  }
}

void nested_documents_and_aliases_share_rows() {
  const auto content = descriptor(uuid(30), sha256('b'));
  const auto inner = basic_document();
  QVariantList child_layers{
      instance_layer(QStringLiteral("child-a"), content, inner),
      instance_layer(QStringLiteral("child-b"), content, inner),
      instance_layer(QStringLiteral("child-c"), content, inner)};
  const auto outer = json_document(raw_document(uuid(31), 4, child_layers));
  auto recipe = make_recipe(3, content, outer);
  const auto expected = recipe;
  QString error;
  const auto externalized = table::externalize_component_objects(&recipe, &error);
  check(externalized, "nested shared objects externalize");
  if (!externalized) return;
  const auto objects = recipe.value(QString::fromLatin1(table::kTableField))
                           .toMap().value(QStringLiteral("objects")).toList();
  check(objects.size() == 3,
        "nested aliases store one descriptor and one row for each document identity");
  check(count_type(objects, QString::fromLatin1(table::kDescriptorType)) == 1 &&
            count_type(objects, QString::fromLatin1(table::kDocumentType)) == 2,
        "nested document content is interned independently of instance state");
  const auto inlined = table::inline_component_objects(&recipe, &error);
  check(inlined, "nested shared objects inline");
  if (!inlined) return;
  check(recipe == expected,
        "nested layer aliases and unknown authored fields round-trip exactly");
}

void nested_group_children_share_objects_and_preserve_other_values() {
  const auto content = descriptor(uuid(35), sha256('b'));
  const auto document = basic_document();
  const QVariantList child_layers{
      instance_layer(QStringLiteral("nested-a"), content, document),
      QStringLiteral("preserved-non-layer-child")};
  const QVariantMap group{
      {QStringLiteral("id"), QStringLiteral("group")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("children"), child_layers},
      {QStringLiteral("layers"), child_layers},
      {QStringLiteral("transform"), QVariantMap{
          {QStringLiteral("x"), 12}, {QStringLiteral("y"), -3}}},
      {QStringLiteral("maskIds"), QVariantList{QStringLiteral("group-mask")}},
      {QStringLiteral("authoredField"), QStringLiteral("keep-me")}};
  QVariantMap recipe{{QStringLiteral("layers"), QVariantList{group}},
                     {QStringLiteral("photoLayers"), QVariantList{group}}};
  const auto expected = recipe;
  QString error;
  const auto packed = table::externalize_component_objects(&recipe, &error);
  check(packed, "group child component objects externalize");
  if (!packed) return;
  const auto objects = recipe.value(QString::fromLatin1(table::kTableField))
                           .toMap().value(QStringLiteral("objects")).toList();
  check(objects.size() == 2,
        "components under children/layers share the global object table");
  check(table::has_references(recipe),
        "reference scanner traverses valid group child maps");
  const auto stored_group = recipe.value(QStringLiteral("layers"))
                                .toList().constFirst().toMap();
  const auto stored_child = stored_group.value(QStringLiteral("children"))
                                .toList().constFirst().toMap();
  check(stored_child.contains(QString::fromLatin1(
            table::kDescriptorReferenceField)) &&
            stored_child.contains(QString::fromLatin1(
                table::kDocumentReferenceField)),
        "nested group layer stores references instead of inline payloads");
  const auto inlined = table::inline_component_objects(&recipe, &error);
  check(inlined, "group child component objects inline");
  if (!inlined) return;
  check(recipe == expected,
        "nested group aliases, masks, transforms, and non-map values round-trip");
}

void distinct_revisions_and_conflicts() {
  const auto document = basic_document();
  const auto independent_document = json_document(raw_document(
      uuid(42), 1,
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("independent-pixels")},
                   {QStringLiteral("kind"), QStringLiteral("pixel")},
                   {QStringLiteral("pixelData"), QByteArray::fromHex("aabbccdd")}}}));
  const auto first = descriptor(uuid(40), sha256('c'));
  const auto second_revision = descriptor(uuid(40), sha256('d'));
  const auto second_content = descriptor(uuid(41), sha256('e'));
  QVariantList layers{
      instance_layer(QStringLiteral("a"), first, document),
      instance_layer(QStringLiteral("b"), second_revision, document),
      instance_layer(QStringLiteral("c"), second_content, independent_document)};
  QVariantMap recipe{{QStringLiteral("layers"), layers},
                     {QStringLiteral("photoLayers"), layers}};
  QString error;
  const auto externalized = table::externalize_component_objects(&recipe, &error);
  check(externalized, "different content revisions externalize");
  if (!externalized) return;
  const auto objects = recipe.value(QString::fromLatin1(table::kTableField))
                           .toMap().value(QStringLiteral("objects")).toList();
  check(count_type(objects, QString::fromLatin1(table::kDescriptorType)) == 3,
        "distinct descriptor revisions and content IDs keep separate rows");
  check(count_type(objects, QString::fromLatin1(table::kDocumentType)) == 2,
        "independent document content keeps a separate table row");

  auto conflict_recipe = make_recipe(2, first, document);
  auto conflict_layers = conflict_recipe.value(QStringLiteral("layers")).toList();
  auto conflicting_layer = conflict_layers.at(1).toMap();
  conflicting_layer.insert(QStringLiteral("smartObject"),
                           descriptor(uuid(40), sha256('c'),
                                      QStringLiteral("different-name.png")));
  conflict_layers[1] = conflicting_layer;
  conflict_recipe.insert(QStringLiteral("layers"), conflict_layers);
  conflict_recipe.insert(QStringLiteral("photoLayers"), conflict_layers);
  const auto before_conflict = conflict_recipe;
  check(!table::externalize_component_objects(&conflict_recipe, &error),
        "conflicting descriptor definitions are rejected");
  check(conflict_recipe == before_conflict,
        "descriptor conflict rejection is atomic");

  auto doc_a = raw_document(uuid(50), 5, {});
  auto doc_b = raw_document(uuid(50), 5, {}, QStringLiteral("different"));
  auto document_conflict = make_recipe(2, first, json_document(doc_a));
  auto conflict_doc_layers = document_conflict.value(QStringLiteral("layers")).toList();
  auto second_layer = conflict_doc_layers.at(1).toMap();
  second_layer.insert(QStringLiteral("smartObjectDocument"), json_document(doc_b));
  conflict_doc_layers[1] = second_layer;
  document_conflict.insert(QStringLiteral("layers"), conflict_doc_layers);
  document_conflict.insert(QStringLiteral("photoLayers"), conflict_doc_layers);
  const auto before_document_conflict = document_conflict;
  check(!table::externalize_component_objects(&document_conflict, &error),
        "conflicting document definitions are rejected");
  check(document_conflict == before_document_conflict,
        "document conflict rejection is atomic");
}

void malformed_tables_refs_and_cycles_fail_atomically() {
  const auto original = make_recipe(1, descriptor(uuid(60), sha256('f')),
                                    basic_document());
  QString error;
  auto stored = original;
  const auto externalized = table::externalize_component_objects(&stored, &error);
  check(externalized, "valid data externalizes before malformed cases");
  if (!externalized) return;

  auto wrong_table_version = stored;
  auto table_map = wrong_table_version.value(QString::fromLatin1(table::kTableField)).toMap();
  table_map.insert(QStringLiteral("version"), 2);
  wrong_table_version.insert(QString::fromLatin1(table::kTableField), table_map);
  const auto wrong_table_before = wrong_table_version;
  check(!table::inline_component_objects(&wrong_table_version, &error),
        "unsupported table versions are rejected");
  check(wrong_table_version == wrong_table_before,
        "unsupported table rejection is atomic");

  auto wrong_ref_version = stored;
  auto root_layers = wrong_ref_version.value(QStringLiteral("layers")).toList();
  auto first_layer = root_layers.first().toMap();
  auto reference = first_layer.value(QString::fromLatin1(
      table::kDescriptorReferenceField)).toMap();
  reference.insert(QStringLiteral("version"), 2);
  first_layer.insert(QString::fromLatin1(table::kDescriptorReferenceField), reference);
  root_layers[0] = first_layer;
  wrong_ref_version.insert(QStringLiteral("layers"), root_layers);
  wrong_ref_version.insert(QStringLiteral("photoLayers"), root_layers);
  const auto wrong_ref_before = wrong_ref_version;
  check(!table::inline_component_objects(&wrong_ref_version, &error),
        "unsupported reference versions are rejected");
  check(wrong_ref_version == wrong_ref_before,
        "unsupported reference rejection is atomic");

  auto missing_entry = stored;
  table_map = missing_entry.value(QString::fromLatin1(table::kTableField)).toMap();
  auto objects = table_map.value(QStringLiteral("objects")).toList();
  objects.removeFirst();
  table_map.insert(QStringLiteral("objects"), objects);
  missing_entry.insert(QString::fromLatin1(table::kTableField), table_map);
  const auto missing_before = missing_entry;
  check(!table::inline_component_objects(&missing_entry, &error),
        "dangling references are rejected");
  check(missing_entry == missing_before,
        "dangling reference rejection is atomic");

  auto orphan = stored;
  root_layers = orphan.value(QStringLiteral("layers")).toList();
  for (auto& value : root_layers) {
    auto layer = value.toMap();
    layer.remove(QString::fromLatin1(table::kDescriptorReferenceField));
    layer.remove(QString::fromLatin1(table::kDocumentReferenceField));
    value = layer;
  }
  orphan.insert(QStringLiteral("layers"), root_layers);
  orphan.insert(QStringLiteral("photoLayers"), root_layers);
  const auto orphan_before = orphan;
  check(!table::inline_component_objects(&orphan, &error),
        "unreferenced table rows are rejected");
  check(orphan == orphan_before, "orphan rejection is atomic");

  const QVariantMap repeated_identity = raw_document(uuid(70), 2, {});
  const QVariantList cycle_layer{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("self")},
                  {QStringLiteral("smartObjectDocument"), repeated_identity}}};
  auto cycle_recipe = make_recipe(
      1, descriptor(uuid(72), sha256('7')),
      json_document(raw_document(uuid(70), 1, cycle_layer)));
  const auto cycle_before = cycle_recipe;
  check(!table::externalize_component_objects(&cycle_recipe, &error),
        "nested document cycles are rejected");
  check(cycle_recipe == cycle_before, "cycle rejection is atomic");

  QVariantMap over_depth_document = raw_document(uuid(89), 1, {});
  for (int id = 88; id >= 80; --id) {
    const QVariantList nested_layer{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("nested")},
                    {QStringLiteral("smartObjectDocument"),
                     over_depth_document}}};
    over_depth_document = raw_document(uuid(id), 1, nested_layer);
  }
  auto depth_recipe = make_recipe(1, descriptor(uuid(90), sha256('9')),
                                  json_document(over_depth_document));
  const auto depth_before = depth_recipe;
  check(!table::externalize_component_objects(&depth_recipe, &error),
        "over-depth nested documents are rejected");
  check(depth_recipe == depth_before, "depth rejection is atomic");
}

void missing_table_and_reference_presence() {
  auto stored = make_recipe(1, descriptor(uuid(80), sha256('8')), basic_document());
  QString error;
  const auto externalized = table::externalize_component_objects(&stored, &error);
  check(externalized, "valid recipe externalizes for missing-table case");
  if (!externalized) return;
  check(table::has_references(stored), "reference scanner finds root references");
  stored.remove(QString::fromLatin1(table::kTableField));
  const auto before = stored;
  check(!table::inline_component_objects(&stored, &error),
        "references without a table are rejected");
  check(stored == before, "missing table rejection is atomic");
}

}  // namespace

// An opt-in workload for many placements sharing a complex nested document.
// Ordinary CTest keeps the existing correctness suite inexpensive.
int document_workload(const QString& output_path) {
  QVariantList mask_nodes;
  for (int i = 0; i < 150; ++i)
    mask_nodes.append(QVariantMap{{QStringLiteral("id"), i},
        {QStringLiteral("opacity"), 0.73},
        {QStringLiteral("curve"), QVariantList{0.0, 0.2, 0.8, 1.0}},
        {QStringLiteral("style"), QVariantMap{{QStringLiteral("color"), "#804020"},
            {QStringLiteral("radius"), 17.5}}}});
  auto leaf = basic_document();
  leaf.insert(QStringLiteral("maskStack"), mask_nodes);
  const auto content = descriptor(uuid(10), sha256('a'));
  const auto nested = instance_layer(QStringLiteral("child"), content, leaf);
  auto document = json_document(raw_document(uuid(40), 1, {nested}));
  QJsonObject result;
  for (const auto count : {1, 80}) {
    const auto original = make_recipe(count, content, document);
    QJsonArray pack_times, inflate_times;
    QByteArray expected;
    for (int iteration = 0; iteration < 61; ++iteration) {
      auto stored = original;
      QString error;
      QElapsedTimer timer;
      timer.start();
      if (!table::externalize_component_objects(&stored, &error)) {
        std::cerr << error.toStdString() << '\n'; return 2;
      }
      pack_times.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      const auto bytes = QJsonDocument::fromVariant(stored).toJson(QJsonDocument::Compact);
      if (expected.isEmpty()) expected = bytes;
      if (bytes != expected) return 3;
      timer.restart();
      if (!table::inline_component_objects(&stored, &error)) return 4;
      inflate_times.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (!table::detail::exact_variant(stored, original)) return 5;
    }
    result.insert(QString::number(count), QJsonObject{
        {QStringLiteral("pack_ms"), pack_times}, {QStringLiteral("inflate_ms"), inflate_times},
        {QStringLiteral("stored_bytes"), expected.size()}});
  }
#if defined(__unix__) || defined(__APPLE__)
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  constexpr double rss_scale = 1.0;
#else
  constexpr double rss_scale = 1024.0;
#endif
  result.insert(QStringLiteral("peak_rss_bytes"), static_cast<double>(usage.ru_maxrss) * rss_scale);
#endif
  result.insert(QStringLiteral("exact_round_trip"), true);
  QFile output(output_path);
  if (!output.open(QIODevice::WriteOnly)) return 6;
  output.write(QJsonDocument(result).toJson());
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 3 && QByteArray(argv[1]) == "--workload")
    return document_workload(QString::fromLocal8Bit(argv[2]));

  const QVariantMap shared{{QStringLiteral("nested"), QVariantList{
      QVariantMap{{QStringLiteral("value"), 1}}, 2}}};
  auto detached = shared;
  detached.insert(QStringLiteral("nested"), QVariantList{
      QVariantMap{{QStringLiteral("value"), 1.0}}, 2});
  check(table::detail::exact_variant(shared, shared), "shared maps compare exactly");
  check(!table::detail::exact_variant(shared, detached),
        "detached nested numeric types remain distinct");
  check(!table::detail::exact_variant(QVariantList{1}, QVariantList{1.0}),
        "distinct lists retain exact scalar types");
  shared_instance_counts_round_trip();
  recipe_json_growth_receipt();
  nested_documents_and_aliases_share_rows();
  nested_group_children_share_objects_and_preserve_other_values();
  distinct_revisions_and_conflicts();
  malformed_tables_refs_and_cycles_fail_atomically();
  missing_table_and_reference_presence();
  if (failures != 0) {
    std::cerr << failures << " component object table checks failed\n";
    return 1;
  }
  std::cout << "component object table checks passed\n";
  return 0;
}
