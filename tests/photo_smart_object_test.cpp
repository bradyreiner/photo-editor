#include "photo_smart_object.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using namespace photo_editor::desktop::smart_object;

namespace {

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) &&
         file.write(bytes) == static_cast<qint64>(bytes.size());
}

QVariantMap source_layer(const QString& id, const QString& path) {
  return QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("name"), QStringLiteral("Product source")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("sourcePath"), path},
      {QStringLiteral("pixelWidth"), 640},
      {QStringLiteral("pixelHeight"), 480},
      {QStringLiteral("x"), 123},
      {QStringLiteral("y"), -27},
      {QStringLiteral("scale"), 0.75},
      {QStringLiteral("rotation"), 13.5},
      {QStringLiteral("opacity"), 0.82},
      {QStringLiteral("blendMode"), QStringLiteral("multiply")},
      {QStringLiteral("locked"), false},
      {QStringLiteral("colorAdjustment"), QVariantMap{{QStringLiteral("exposure"), 0.4}}},
      {QStringLiteral("layerMask"), QVariantMap{{QStringLiteral("id"), QStringLiteral("mask-a")},
                                                 {QStringLiteral("pixels"), QByteArray::fromHex("000102ff")}}},
      {QStringLiteral("effects"), QVariantList{QVariantMap{{QStringLiteral("kind"), QStringLiteral("shadow")},
                                                           {QStringLiteral("radius"), 8.0}}}},
      {QStringLiteral("layerStyle"), QVariantMap{{QStringLiteral("stroke"), QStringLiteral("#f4a030")}}}};
}

QVariantMap without_source_fields(QVariantMap layer) {
  layer.remove(QStringLiteral("smartObject"));
  layer.remove(QStringLiteral("smartObjectInstanceId"));
  layer.remove(QStringLiteral("smartObjectIntrinsicWidth"));
  layer.remove(QStringLiteral("smartObjectIntrinsicHeight"));
  layer.remove(QStringLiteral("sourcePath"));
  layer.remove(QStringLiteral("pixelData"));
  layer.remove(QStringLiteral("pixelDataEncoding"));
  return layer;
}

QVariantMap nested_layer(const QVariantList& stack, const QString& id) {
  for (const auto& value : stack) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
    for (const auto& child : layer.value(QStringLiteral("children")).toList()) {
      const auto nested = child.toMap();
      if (nested.value(QStringLiteral("id")).toString() == id) return nested;
    }
    for (const auto& child : layer.value(QStringLiteral("layers")).toList()) {
      const auto nested = child.toMap();
      if (nested.value(QStringLiteral("id")).toString() == id) return nested;
    }
  }
  return {};
}

QVariantList replace_nested_layer(QVariantList stack, const QString& id,
                                  const QVariantMap& replacement) {
  for (auto& value : stack) {
    auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) {
      layer = replacement;
    } else if (layer.contains(QStringLiteral("children"))) {
      layer.insert(QStringLiteral("children"), replace_nested_layer(
          layer.value(QStringLiteral("children")).toList(), id, replacement));
    } else if (layer.contains(QStringLiteral("layers"))) {
      layer.insert(QStringLiteral("layers"), replace_nested_layer(
          layer.value(QStringLiteral("layers")).toList(), id, replacement));
    }
    value = layer;
  }
  return stack;
}

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;

  const auto original_bytes = QByteArray("source\0bytes\xff\x01", 14);
  const auto replacement_bytes = QByteArray("replacement-content\x00v2", 22);
  const auto third_bytes = QByteArray("third-content\x00v3", 17);
  const auto original_path = temporary.filePath(QStringLiteral("original.raw"));
  const auto replacement_path = temporary.filePath(QStringLiteral("replacement.raw"));
  const auto third_path = temporary.filePath(QStringLiteral("third.raw"));
  if (!write_bytes(original_path, original_bytes) ||
      !write_bytes(replacement_path, replacement_bytes) ||
      !write_bytes(third_path, third_bytes)) return 1;

  QString error;
  const auto original_source = source_layer(QStringLiteral("source-template"), original_path);
  const auto embedded = create_content_descriptor(original_source, ContentPolicy::Embedded, &error);
  if (!check(!embedded.isEmpty() && error.isEmpty(), "embedded source descriptor creation failed")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto embedded_status = inspect_link_status(embedded, &error);
  if (!check(error.isEmpty() && embedded_status.value(QStringLiteral("status")).toString() ==
                 QStringLiteral("embedded"),
             "embedded source status inspection was not reported as portable content")) return 1;

  const auto serialized = serialize_content_descriptor(embedded, &error);
  const auto reopened = deserialize_content_descriptor(serialized, &error);
  const auto serialized_again = serialize_content_descriptor(reopened, &error);
  if (!check(!serialized.isEmpty() && serialized == serialized_again && error.isEmpty(),
             "Smart Object descriptor JSON did not round-trip byte-for-byte")) return 1;
  if (!check(read_content_bytes(reopened, &error) == original_bytes && error.isEmpty(),
             "embedded source payload bytes changed during serialization")) return 1;

  auto first = source_layer(QStringLiteral("instance-a"), original_path);
  first.insert(QStringLiteral("transform"), QVariantMap{{QStringLiteral("anchorX"), 0.3},
                                                         {QStringLiteral("anchorY"), 0.7}});
  auto second = source_layer(QStringLiteral("instance-b"), original_path);
  second.insert(QStringLiteral("transform"), QVariantMap{{QStringLiteral("anchorX"), 0.8},
                                                          {QStringLiteral("anchorY"), 0.2}});
  auto ordinary = source_layer(QStringLiteral("ordinary"), original_path);
  ordinary.insert(QStringLiteral("locked"), true);
  const QVariantMap group{
      {QStringLiteral("id"), QStringLiteral("group-a")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("locked"), false},
      {QStringLiteral("children"), QVariantList{first, second}}};
  const QVariantList initial_stack{group, ordinary};

  const auto instances = create_instances(initial_stack,
      {QStringLiteral("instance-a"), QStringLiteral("instance-b")}, embedded, &error);
  const auto instance_a = nested_layer(instances, QStringLiteral("instance-a"));
  const auto instance_b = nested_layer(instances, QStringLiteral("instance-b"));
  const auto shared_id = embedded.value(QStringLiteral("contentId")).toString();
  if (!check(error.isEmpty() && !instance_a.isEmpty() && !instance_b.isEmpty() &&
             instance_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id &&
             instance_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id,
             "selected source layers did not become members of one reusable content group")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  if (!check(instance_a.value(QStringLiteral("smartObjectInstanceId")).toString() !=
                 instance_b.value(QStringLiteral("smartObjectInstanceId")).toString(),
             "instances must retain distinct per-layer instance IDs")) return 1;
  if (!check(without_source_fields(instance_a) == without_source_fields(first) &&
                 without_source_fields(instance_b) == without_source_fields(second),
             "converting layers changed placement, masks, effects, color, or styles")) return 1;
  const auto resolved_embedded = resolve_instance_source(instance_a, &error);
  if (!check(error.isEmpty() && resolved_embedded.value(QStringLiteral("pixelData")).toByteArray() == original_bytes &&
                 resolved_embedded.value(QStringLiteral("pixelDataEncoding")).toString() == QStringLiteral("source-file") &&
                 resolved_embedded.value(QStringLiteral("pixelWidth")).toInt() == 640 &&
                 resolved_embedded.value(QStringLiteral("pixelHeight")).toInt() == 480 &&
                 resolved_embedded.value(QStringLiteral("smartObjectIntrinsicWidth")).toInt() == 640,
             "embedded instance source resolver did not expose its source bytes and intrinsic dimensions")) return 1;

  auto replacement_source = source_layer(QStringLiteral("replacement-template"), replacement_path);
  replacement_source.insert(QStringLiteral("pixelWidth"), 900);
  replacement_source.insert(QStringLiteral("pixelHeight"), 600);
  const auto replacement = create_content_descriptor(replacement_source,
      ContentPolicy::Embedded, &error);
  const auto replaced = replace_contents(instances, {QStringLiteral("instance-a")}, replacement, &error);
  const auto replaced_a = nested_layer(replaced, QStringLiteral("instance-a"));
  const auto replaced_b = nested_layer(replaced, QStringLiteral("instance-b"));
  if (!check(error.isEmpty() &&
                 replaced_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) == replacement.value(QStringLiteral("sourceSha256")) &&
                 replaced_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) == replacement.value(QStringLiteral("sourceSha256")) &&
                 replaced_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id &&
                 replaced_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id,
             "Replace Contents did not update every shared instance while preserving the logical content ID")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  if (!check(without_source_fields(replaced_a) == without_source_fields(first) &&
                 without_source_fields(replaced_b) == without_source_fields(second),
             "Replace Contents changed an outer placement or style field")) return 1;
  if (!check(replaced_a.value(QStringLiteral("pixelWidth")).toInt() == 640 &&
                 replaced_a.value(QStringLiteral("pixelHeight")).toInt() == 480 &&
                 replaced_a.value(QStringLiteral("smartObjectIntrinsicWidth")).toInt() == 900 &&
                 replaced_a.value(QStringLiteral("smartObjectIntrinsicHeight")).toInt() == 600,
             "replacement did not keep placement bounds separate from source intrinsic dimensions")) return 1;

  const auto independent = make_independent(replaced, {QStringLiteral("instance-b")}, &error);
  const auto independent_a = nested_layer(independent, QStringLiteral("instance-a"));
  const auto independent_b = nested_layer(independent, QStringLiteral("instance-b"));
  const auto independent_id = independent_b.value(QStringLiteral("smartObject")).toMap()
                                  .value(QStringLiteral("contentId")).toString();
  if (!check(error.isEmpty() && independent_id != shared_id &&
                 independent_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id &&
                 independent_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) == replacement.value(QStringLiteral("sourceSha256")) &&
                 without_source_fields(independent_b) == without_source_fields(second),
             "Make Independent did not isolate only the chosen instance")) return 1;

  auto third_source = source_layer(QStringLiteral("third-template"), third_path);
  const auto third_descriptor = create_content_descriptor(third_source, ContentPolicy::Embedded, &error);
  const auto second_replacement = replace_contents(independent, {QStringLiteral("instance-a")}, third_descriptor, &error);
  const auto final_a = nested_layer(second_replacement, QStringLiteral("instance-a"));
  const auto final_b = nested_layer(second_replacement, QStringLiteral("instance-b"));
  if (!check(error.isEmpty() &&
                 final_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) == third_descriptor.value(QStringLiteral("sourceSha256")) &&
                 final_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) == replacement.value(QStringLiteral("sourceSha256")) &&
                 final_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == independent_id,
             "replacement after Make Independent changed the isolated instance")) return 1;

  const auto clone = create_instance(first, embedded, QStringLiteral("instance-clone"), &error);
  auto expected_clone_outer = first;
  expected_clone_outer.insert(QStringLiteral("id"), QStringLiteral("instance-clone"));
  if (!check(error.isEmpty() && clone.value(QStringLiteral("id")).toString() == QStringLiteral("instance-clone") &&
                 clone.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == shared_id &&
                 without_source_fields(clone) == without_source_fields(expected_clone_outer),
             "creating a new instance did not preserve the template layer's outer fields")) return 1;

  auto locked_sibling = nested_layer(instances, QStringLiteral("instance-b"));
  locked_sibling.insert(QStringLiteral("locked"), true);
  auto locked_stack = replace_nested_layer(instances, QStringLiteral("instance-b"), locked_sibling);
  const auto locked_result = replace_contents(locked_stack,
      {QStringLiteral("instance-a")}, replacement, &error);
  if (!check(!error.isEmpty() && locked_result == locked_stack,
             "replacement was not rejected atomically when a shared sibling was locked")) return 1;

  auto locked_group = instances.at(0).toMap();
  locked_group.insert(QStringLiteral("locked"), true);
  auto locked_parent_stack = instances;
  locked_parent_stack[0] = locked_group;
  const auto locked_parent_result = make_independent(locked_parent_stack,
      {QStringLiteral("instance-a")}, &error);
  if (!check(!error.isEmpty() && locked_parent_result == locked_parent_stack,
             "Make Independent was not rejected atomically below a locked parent group")) return 1;

  auto invalid_replacement = replacement;
  invalid_replacement.insert(QStringLiteral("sourceSha256"), QString(64, QLatin1Char('0')));
  const auto invalid_result = replace_contents(instances,
      {QStringLiteral("instance-a")}, invalid_replacement, &error);
  if (!check(!error.isEmpty() && invalid_result == instances,
             "invalid replacement content changed the stack or lacked an error")) {
    std::cerr << "error=" << error.toStdString()
              << " unchanged=" << (invalid_result == instances) << '\n';
    return 1;
  }
  auto nested_reference = replacement;
  nested_reference.insert(QStringLiteral("nestedDocumentRef"), shared_id);
  const auto nested_reference_result = replace_contents(instances,
      {QStringLiteral("instance-a")}, nested_reference, &error);
  if (!check(!error.isEmpty() && nested_reference_result == instances &&
                 error.contains(QStringLiteral("unsupported field")),
             "v1 source content accepted an unsupported nested-document reference")) return 1;
  auto corrupt_payload = replacement;
  corrupt_payload.insert(QStringLiteral("dataBase64"),
                         QString::fromLatin1(QByteArray("corrupt-bytes").toBase64()));
  const auto corrupt_result = replace_contents(instances,
      {QStringLiteral("instance-a")}, corrupt_payload, &error);
  if (!check(!error.isEmpty() && corrupt_result == instances,
             "corrupt embedded payload changed the stack or lacked an error")) return 1;

  auto conflicting_sibling = nested_layer(instances, QStringLiteral("instance-b"));
  auto conflicting_descriptor = third_descriptor;
  conflicting_descriptor.insert(QStringLiteral("contentId"), shared_id);
  conflicting_sibling.insert(QStringLiteral("smartObject"), conflicting_descriptor);
  auto conflicting_stack = replace_nested_layer(instances,
      QStringLiteral("instance-b"), conflicting_sibling);
  const auto conflicting_result = replace_contents(conflicting_stack,
      {QStringLiteral("instance-a")}, replacement, &error);
  if (!check(!error.isEmpty() && conflicting_result == conflicting_stack &&
                 error.contains(QStringLiteral("conflicting content descriptors")),
             "conflicting descriptors with one contentId were not rejected atomically")) return 1;

  const auto linked_path = temporary.filePath(QStringLiteral("linked.raw"));
  if (!write_bytes(linked_path, original_bytes)) return 1;
  const auto linked_descriptor = create_content_descriptor(
      source_layer(QStringLiteral("linked-template"), linked_path), ContentPolicy::Linked, &error);
  if (!check(!linked_descriptor.isEmpty() && error.isEmpty(), "linked descriptor creation failed")) return 1;
  const auto linked_layer = create_instance(first, linked_descriptor,
      QStringLiteral("linked-instance"), &error);
  if (!check(error.isEmpty() && linked_layer.value(QStringLiteral("sourcePath")).toString() ==
                 QFileInfo(linked_path).absoluteFilePath(),
             "linked instance resolver did not expose the canonical source path")) return 1;
  const auto current_link_status = inspect_link_status(linked_descriptor, &error);
  if (!check(error.isEmpty() && current_link_status.value(QStringLiteral("status")).toString() ==
                 QStringLiteral("current") &&
                 current_link_status.value(QStringLiteral("actualSha256")) ==
                     linked_descriptor.value(QStringLiteral("sourceSha256")),
             "linked status inspection did not recognize matching source bytes")) return 1;
  auto changed_link_bytes = original_bytes;
  changed_link_bytes[0] = static_cast<char>(changed_link_bytes.at(0) ^ 0x7f);
  if (!write_bytes(linked_path, changed_link_bytes)) return 1;
  const auto changed_link_status = inspect_link_status(linked_descriptor, &error);
  if (!check(error.isEmpty() && changed_link_status.value(QStringLiteral("status")).toString() ==
                 QStringLiteral("changed"),
             "linked status inspection did not report a changed source revision")) return 1;
  const auto changed_link = resolve_instance_source(linked_layer, &error);
  if (!check(changed_link.isEmpty() && error.contains(QStringLiteral("changed")),
             "linked content revision mismatch was not detected")) return 1;

  // Refresh accepts a newer on-disk revision and updates every shared member
  // while keeping each placement, layer ID, instance ID, and group identity.
  const auto shared_link_path = temporary.filePath(QStringLiteral("shared-linked.raw"));
  if (!write_bytes(shared_link_path, original_bytes)) return 1;
  const auto shared_link_descriptor = create_content_descriptor(
      source_layer(QStringLiteral("shared-linked-template"), shared_link_path),
      ContentPolicy::Linked, &error);
  const auto linked_first = source_layer(QStringLiteral("link-instance-a"), shared_link_path);
  auto linked_second = source_layer(QStringLiteral("link-instance-b"), shared_link_path);
  linked_second.insert(QStringLiteral("transform"), QVariantMap{
      {QStringLiteral("anchorX"), 0.27}, {QStringLiteral("anchorY"), 0.84}});
  const QVariantList linked_stack{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("linked-group")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("children"), QVariantList{linked_first, linked_second}}}};
  const auto linked_instances = create_instances(linked_stack,
      {QStringLiteral("link-instance-a"), QStringLiteral("link-instance-b")},
      shared_link_descriptor, &error);
  const auto link_a = nested_layer(linked_instances, QStringLiteral("link-instance-a"));
  const auto link_b = nested_layer(linked_instances, QStringLiteral("link-instance-b"));
  const auto linked_content_id = shared_link_descriptor.value(QStringLiteral("contentId")).toString();
  const auto link_a_instance_id = link_a.value(QStringLiteral("smartObjectInstanceId")).toString();
  const auto link_b_instance_id = link_b.value(QStringLiteral("smartObjectInstanceId")).toString();
  auto shared_link_changed_bytes = original_bytes;
  shared_link_changed_bytes[1] = static_cast<char>(shared_link_changed_bytes.at(1) ^ 0x3d);
  if (!write_bytes(shared_link_path, shared_link_changed_bytes)) return 1;
  if (!check(error.isEmpty() && !link_a.isEmpty() && !link_b.isEmpty(),
             "linked test instances were not created")) return 1;
  const auto refreshed = refresh_linked_contents(linked_instances,
      {QStringLiteral("link-instance-a")}, 700, 500, &error);
  const auto refreshed_a = nested_layer(refreshed, QStringLiteral("link-instance-a"));
  const auto refreshed_b = nested_layer(refreshed, QStringLiteral("link-instance-b"));
  if (!check(error.isEmpty() &&
                 refreshed_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) ==
                     QString::fromLatin1(QCryptographicHash::hash(shared_link_changed_bytes, QCryptographicHash::Sha256).toHex()) &&
                 refreshed_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) ==
                     refreshed_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("sourceSha256")) &&
                 refreshed_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() ==
                     linked_content_id &&
                 refreshed_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("intrinsicWidth")).toInt() == 700 &&
                 refreshed_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("intrinsicHeight")).toInt() == 500 &&
                 refreshed_a.value(QStringLiteral("pixelWidth")).toInt() == 640 &&
                 refreshed_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() ==
                     linked_content_id &&
                 refreshed_a.value(QStringLiteral("smartObjectInstanceId")).toString() == link_a_instance_id &&
                 refreshed_b.value(QStringLiteral("smartObjectInstanceId")).toString() == link_b_instance_id &&
                 without_source_fields(refreshed_a) == without_source_fields(link_a) &&
                 without_source_fields(refreshed_b) == without_source_fields(link_b),
             "Refresh did not update the complete linked group while preserving identities and placement state")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  auto locked_link_sibling = nested_layer(refreshed, QStringLiteral("link-instance-b"));
  locked_link_sibling.insert(QStringLiteral("locked"), true);
  const auto locked_link_stack = replace_nested_layer(
      refreshed, QStringLiteral("link-instance-b"), locked_link_sibling);
  const auto locked_refresh = refresh_linked_contents(locked_link_stack,
      {QStringLiteral("link-instance-a")}, &error);
  if (!check(!error.isEmpty() && locked_refresh == locked_link_stack,
             "linked refresh changed part of a group whose sibling was locked")) return 1;

  const auto relinked = relink_contents(refreshed,
      {QStringLiteral("link-instance-a")}, third_path, 1200, 800, &error);
  const auto relinked_a = nested_layer(relinked, QStringLiteral("link-instance-a"));
  const auto relinked_b = nested_layer(relinked, QStringLiteral("link-instance-b"));
  if (!check(error.isEmpty() &&
                 relinked_a.value(QStringLiteral("sourcePath")).toString() == QFileInfo(third_path).absoluteFilePath() &&
                 relinked_b.value(QStringLiteral("sourcePath")).toString() == QFileInfo(third_path).absoluteFilePath() &&
                 relinked_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == linked_content_id &&
                 relinked_b.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("contentId")).toString() == linked_content_id &&
                 relinked_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("intrinsicWidth")).toInt() == 1200 &&
                 relinked_a.value(QStringLiteral("smartObject")).toMap().value(QStringLiteral("intrinsicHeight")).toInt() == 800 &&
                 without_source_fields(relinked_a) == without_source_fields(link_a) &&
                 without_source_fields(relinked_b) == without_source_fields(link_b),
             "Relink did not atomically retarget every shared instance and preserve placement state")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto embedded_links = embed_linked_contents(relinked,
      {QStringLiteral("link-instance-b")}, &error);
  const auto embedded_link_a = nested_layer(embedded_links, QStringLiteral("link-instance-a"));
  const auto embedded_link_b = nested_layer(embedded_links, QStringLiteral("link-instance-b"));
  const auto embedded_link_descriptor = embedded_link_a.value(QStringLiteral("smartObject")).toMap();
  if (!check(error.isEmpty() &&
                 embedded_link_descriptor.value(QStringLiteral("policy")).toString() == QStringLiteral("embedded") &&
                 embedded_link_descriptor.value(QStringLiteral("sourcePath")).toString().isEmpty() &&
                 embedded_link_descriptor.value(QStringLiteral("contentId")).toString() == linked_content_id &&
                 embedded_link_b.value(QStringLiteral("smartObject")).toMap() == embedded_link_descriptor &&
                 read_content_bytes(embedded_link_descriptor, &error) == third_bytes && error.isEmpty() &&
                 without_source_fields(embedded_link_a) == without_source_fields(link_a) &&
                 without_source_fields(embedded_link_b) == without_source_fields(link_b),
             "Embedding did not make the shared linked group portable without changing its placements")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto missing_status_descriptor = linked_descriptor;
  if (!QFile::remove(linked_path)) return 1;
  const auto missing_status = inspect_link_status(missing_status_descriptor, &error);
  if (!check(error.isEmpty() && missing_status.value(QStringLiteral("status")).toString() ==
                 QStringLiteral("missing"),
             "linked status inspection did not report a missing source file")) return 1;
  auto unreadable_status_descriptor = missing_status_descriptor;
  unreadable_status_descriptor.insert(QStringLiteral("sourcePath"), temporary.path());
  const auto unreadable_status = inspect_link_status(unreadable_status_descriptor, &error);
  if (!check(error.isEmpty() && unreadable_status.value(QStringLiteral("status")).toString() ==
                 QStringLiteral("unreadable"),
             "linked status inspection did not distinguish an unreadable path from a missing file")) return 1;
  const auto linked_serialized = serialize_content_descriptor(linked_descriptor, &error);
  const auto missing_link_reopened = deserialize_content_descriptor(linked_serialized, &error);
  if (!check(!missing_link_reopened.isEmpty() && error.isEmpty() &&
                 !validate_content_descriptor(missing_link_reopened, true, &error) && !error.isEmpty(),
             "missing linked content did not remain reopenable and report unavailable when resolved")) return 1;
  const auto missing_link_result = replace_contents(instances,
      {QStringLiteral("instance-a")}, missing_link_reopened, &error);
  if (!check(!error.isEmpty() && missing_link_result == instances,
             "missing linked replacement changed shared instances")) return 1;

  auto paint_layer = QVariantMap{{QStringLiteral("id"), QStringLiteral("paint")},
                                 {QStringLiteral("kind"), QStringLiteral("pixel")},
                                 {QStringLiteral("paintTiles"), QVariantMap{}}};
  const auto unsupported = create_content_descriptor(paint_layer, ContentPolicy::Embedded, &error);
  if (!check(unsupported.isEmpty() && error.contains(QStringLiteral("worker-side rasterization")),
             "paint tile conversion did not fail with its explicit materialization gate")) return 1;

  QVariantList deep_stack{source_layer(QStringLiteral("deep-leaf"), original_path)};
  for (int depth = 0; depth < 66; ++depth) {
    deep_stack = {QVariantMap{{QStringLiteral("id"), QStringLiteral("deep-group-%1").arg(depth)},
                              {QStringLiteral("kind"), QStringLiteral("group")},
                              {QStringLiteral("children"), deep_stack}}};
  }
  const auto excessive_depth = make_independent(deep_stack,
      {QStringLiteral("deep-leaf")}, &error);
  if (!check(!error.isEmpty() && excessive_depth == deep_stack &&
                 error.contains(QStringLiteral("64 levels")),
             "excessively nested layer stack did not fail safely before mutation")) return 1;

  std::cout << "Smart Object source descriptors round-trip exactly; linked status, atomic refresh/relink/embed, shared identities, placement/style preservation, lock rejection, corrupt payloads, and unsupported nesting pass.\n";
  return 0;
}
