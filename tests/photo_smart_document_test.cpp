#include "photo_smart_document.h"
#include "photo_paint_surface.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImage>
#include <QUuid>
#include <QtGui/qrgbafloat.h>

#include <iostream>
#include <limits>

using namespace photo_editor::desktop::smart_object::nested_document;
using photo_editor::desktop::PhotoPaintSurface;
using photo_editor::desktop::empty_photo_paint_tiles;

namespace {

QString new_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

QVariantMap embedded_source_descriptor() {
  const QByteArray source_bytes = QByteArray::fromHex("89504e470d0a1a0a") +
                                  QByteArray("small embedded image bytes");
  return QVariantMap{
      {QStringLiteral("schema"), QStringLiteral("looksy.smart-object.source")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("contentId"), new_id()},
      {QStringLiteral("policy"), QStringLiteral("embedded")},
      {QStringLiteral("payloadKind"), QStringLiteral("source-file")},
      {QStringLiteral("sourceSha256"), QString::fromLatin1(
          QCryptographicHash::hash(source_bytes, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("byteLength"), source_bytes.size()},
      {QStringLiteral("fileName"), QStringLiteral("product.png")},
      {QStringLiteral("intrinsicWidth"), 1200},
      {QStringLiteral("intrinsicHeight"), 800},
      {QStringLiteral("sourcePath"), QString()},
      {QStringLiteral("dataBase64"), QString::fromLatin1(source_bytes.toBase64())}};
}

QVariantMap mask_definition() {
  return QVariantMap{
      {QStringLiteral("id"), QStringLiteral("mask-definition-a")},
      {QStringLiteral("maskSetId"), QStringLiteral("mask-set-a")},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("maskData"), QByteArray::fromHex("000102ff")}};
}

QVariantMap component_retouch_mask(const QString& id, const QString& mode) {
  return QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("repairId"), id},
      {QStringLiteral("toolRole"), QStringLiteral("retouch")},
      {QStringLiteral("retouchMode"), mode},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("componentPostCompositeRetouchVersion"), 1},
      {QStringLiteral("componentRetouchCanvasWidth"), 64},
      {QStringLiteral("componentRetouchCanvasHeight"), 48}};
}

QVariantMap painted_pixel_layer(const QString& id = QStringLiteral("painted-pixel")) {
  QImage tile(QSize(2, 2), QImage::Format_RGBA32FPx4_Premultiplied);
  tile.fill(Qt::transparent);
  auto* first_row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(0));
  first_row[0] = QRgbaFloat32{4.0F, 0.25F, -0.1F, 0.5F};
  first_row[1] = QRgbaFloat32{0.1F, 0.2F, 0.3F, 1.0F};
  auto paint = empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear));
  paint.insert(QStringLiteral("tiles"), QVariantMap{
      {QStringLiteral("0,0"), PhotoPaintSurface::encode_tile(tile)}});
  return QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), 2},
      {QStringLiteral("pixelHeight"), 2},
      {QStringLiteral("paintTiles"), paint}};
}

QVariantMap component_source_tree() {
  QVariantMap fill{
      {QStringLiteral("id"), QStringLiteral("component-fill")},
      {QStringLiteral("parentId"), QStringLiteral("component-group")},
      {QStringLiteral("kind"), QStringLiteral("fill")},
      {QStringLiteral("fillColor"), QStringLiteral("#d06040")},
      {QStringLiteral("selectionMaskSetId"), QStringLiteral("mask-set-a")},
      {QStringLiteral("opacity"), 0.8}};
  const QVariantMap text{
      {QStringLiteral("id"), QStringLiteral("component-title")},
      {QStringLiteral("parentId"), QStringLiteral("component-group")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("Editable product title")},
      {QStringLiteral("fontFamily"), QStringLiteral("Helvetica Neue")},
      {QStringLiteral("fontSize"), 46.0}};
  const QVariantMap vector{
      {QStringLiteral("id"), QStringLiteral("component-vector")},
      {QStringLiteral("parentId"), QStringLiteral("component-group")},
      {QStringLiteral("kind"), QStringLiteral("shape")},
      {QStringLiteral("shapeDefinition"), QVariantMap{
          {QStringLiteral("schema"), QStringLiteral("shape-v1")},
          {QStringLiteral("path"), QStringLiteral("M0 0 L1 0 L1 1 Z")}}},
      {QStringLiteral("layerStyle"), QVariantMap{
          {QStringLiteral("stroke"), QStringLiteral("#ffffff")}}}};
  const QVariantMap pixel{
      {QStringLiteral("id"), QStringLiteral("component-pixel")},
      {QStringLiteral("parentId"), QStringLiteral("component-group")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), 2},
      {QStringLiteral("pixelHeight"), 2},
      {QStringLiteral("pixelData"), QByteArray::fromHex("ff0000ff00ff00ff0000ffffffffffff")},
      {QStringLiteral("maskData"), QByteArray::fromHex("00ff00ff")}};
  const QVariantMap image{
      {QStringLiteral("id"), QStringLiteral("component-image")},
      {QStringLiteral("parentId"), QStringLiteral("component-group")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("smartObject"), embedded_source_descriptor()},
      {QStringLiteral("smartObjectInstanceId"), new_id()},
      {QStringLiteral("pixelWidth"), 1200},
      {QStringLiteral("pixelHeight"), 800}};
  return QVariantMap{
      {QStringLiteral("id"), QStringLiteral("component-group")},
      {QStringLiteral("name"), QStringLiteral("Product Component")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("visible"), true},
      {QStringLiteral("locked"), false},
      {QStringLiteral("opacity"), 0.7},
      {QStringLiteral("blendMode"), QStringLiteral("multiply")},
      {QStringLiteral("layerStyle"), QVariantMap{
          {QStringLiteral("shadow"), QStringLiteral("soft")}}},
      {QStringLiteral("children"), QVariantList{fill, text, vector, pixel, image}}};
}

QVariantList source_stack() {
  return QVariantList{
      component_source_tree(),
      QVariantMap{{QStringLiteral("id"), QStringLiteral("outside-fill")},
                  {QStringLiteral("kind"), QStringLiteral("fill")},
                  {QStringLiteral("fillColor"), QStringLiteral("#101820")}}};
}

QVariantList flat_group_stack() {
  const auto group_id = QStringLiteral("flat-component-group");
  return QVariantList{
      QVariantMap{{QStringLiteral("id"), group_id},
                  {QStringLiteral("name"), QStringLiteral("Flat Component")},
                  {QStringLiteral("kind"), QStringLiteral("group")},
                  {QStringLiteral("locked"), false}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("flat-fill")},
                  {QStringLiteral("parentId"), group_id},
                  {QStringLiteral("kind"), QStringLiteral("fill")},
                  {QStringLiteral("fillColor"), QStringLiteral("#a06040")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("flat-text")},
                  {QStringLiteral("parentId"), group_id},
                  {QStringLiteral("kind"), QStringLiteral("text")},
                  {QStringLiteral("text"), QStringLiteral("Flat editable title")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("flat-image")},
                  {QStringLiteral("parentId"), group_id},
                  {QStringLiteral("kind"), QStringLiteral("source")},
                  {QStringLiteral("smartObject"), embedded_source_descriptor()},
                  {QStringLiteral("smartObjectInstanceId"), new_id()}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("flat-unselected-sibling")},
                  {QStringLiteral("kind"), QStringLiteral("fill")},
                  {QStringLiteral("fillColor"), QStringLiteral("#203040")}}};
}

QVariantMap make_document(const QString& content_id,
                          const QVariantList& layers,
                          int revision = 1,
                          const QVariantList& masks = {}) {
  return QVariantMap{
      {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
      {QStringLiteral("version"), kVersion},
      {QStringLiteral("contentId"), content_id},
      {QStringLiteral("revision"), revision},
      {QStringLiteral("canvasWidth"), 64},
      {QStringLiteral("canvasHeight"), 48},
      {QStringLiteral("layers"), layers},
      {QStringLiteral("maskStack"), masks}};
}

QVariantMap component_instance(const QString& id, const QVariantMap& document,
                              double x, bool locked = false) {
  return QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("name"), QStringLiteral("Placed %1").arg(id)},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("smartObjectInstanceId"), new_id()},
      {QStringLiteral("smartObjectDocument"), document},
      {QStringLiteral("x"), x},
      {QStringLiteral("y"), 31.0},
      {QStringLiteral("rotation"), 17.0},
      {QStringLiteral("opacity"), 0.4},
      {QStringLiteral("blendMode"), QStringLiteral("multiply")},
      {QStringLiteral("layerMask"), QVariantMap{{QStringLiteral("pixels"), QByteArray("mask")}}},
      {QStringLiteral("locked"), locked}};
}

QVariantMap without_document_and_identity(QVariantMap layer) {
  layer.remove(QStringLiteral("smartObjectDocument"));
  layer.remove(QStringLiteral("smartObjectInstanceId"));
  layer.remove(QStringLiteral("id"));
  return layer;
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      const auto nested = layer_by_id(layer.value(key).toList(), id);
      if (!nested.isEmpty()) return nested;
    }
  }
  return {};
}

bool contains_thumbnail_key(const QVariant& value) {
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      if (it.key().startsWith(QStringLiteral("thumbnail"), Qt::CaseInsensitive) ||
          contains_thumbnail_key(it.value())) return true;
    }
  } else if (value.metaType().id() == QMetaType::QVariantList) {
    for (const auto& item : value.toList()) {
      if (contains_thumbnail_key(item)) return true;
    }
  }
  return false;
}

QVariantMap replace_in_document(const QVariantMap& document,
                                const QString& layer_id,
                                const QVariantMap& replacement) {
  auto result = document;
  QVariantList layers = result.value(QStringLiteral("layers")).toList();
  for (auto& value : layers) {
    auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == layer_id) {
      value = replacement;
    } else {
      for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
        if (!layer.contains(key)) continue;
        QVariantList children = layer.value(key).toList();
        for (auto& child_value : children) {
          auto child = child_value.toMap();
          if (child.value(QStringLiteral("id")).toString() == layer_id) child_value = replacement;
        }
        layer.insert(key, children);
      }
      value = layer;
    }
  }
  result.insert(QStringLiteral("layers"), layers);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QString error;

  const auto input_stack = source_stack();
  const auto masks = QVariantList{mask_definition()};
  const auto document = document_from_layers(
      input_stack, masks, {QStringLiteral("component-group")}, 64, 48, &error);
  if (!check(!document.isEmpty() && error.isEmpty(), "Selected editable subtree did not create a nested document")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  if (!check(document.value(QStringLiteral("schema")).toString() == QString::fromLatin1(kSchema) &&
                 document.value(QStringLiteral("revision")).toInt() == 1 &&
                 document.value(QStringLiteral("canvasWidth")).toInt() == 64 &&
                 document.value(QStringLiteral("canvasHeight")).toInt() == 48 &&
                 document.value(QStringLiteral("maskStack")).toList() == masks,
             "Nested document did not retain its schema, canvas, revision, and parent mask definitions")) return 1;
  const auto child_group = document.value(QStringLiteral("layers")).toList().first().toMap();
  const auto child_layers = child_group.value(QStringLiteral("children")).toList();
  if (!check(child_layers.size() == 5 &&
                 child_layers.at(0).toMap().value(QStringLiteral("selectionMaskSetId")).toString() ==
                     QStringLiteral("mask-set-a") &&
                 child_layers.at(1).toMap().value(QStringLiteral("text")).toString() ==
                     QStringLiteral("Editable product title") &&
                 child_layers.at(2).toMap().contains(QStringLiteral("shapeDefinition")) &&
                 child_layers.at(3).toMap().value(QStringLiteral("pixelData")).toByteArray() ==
                     QByteArray::fromHex("ff0000ff00ff00ff0000ffffffffffff") &&
                 child_layers.at(4).toMap().value(QStringLiteral("smartObject")).toMap()
                     .value(QStringLiteral("policy")).toString() == QStringLiteral("embedded") &&
                 QByteArray::fromBase64(child_layers.at(4).toMap()
                     .value(QStringLiteral("smartObject")).toMap()
                     .value(QStringLiteral("dataBase64")).toString().toLatin1()) ==
                     QByteArray::fromHex("89504e470d0a1a0a") + QByteArray("small embedded image bytes"),
             "Nested document did not preserve editable fill/text/vector/pixel and embedded image children")) return 1;

  const auto authored_stack = source_stack();
  auto thumbnail_variant_a = authored_stack;
  auto thumbnail_variant_b = authored_stack;
  const auto add_view_state = [](QVariantList* stack, const QByteArray& source,
                                 const QString& failure) {
    auto root = stack->first().toMap();
    root.insert(QStringLiteral("thumbnailSource"), source);
    root.insert(QStringLiteral("thumbnailQueueState"), QVariantMap{
        {QStringLiteral("thumbnailInvalidationGeneration"), source.size()},
        {QStringLiteral("thumbnailFailure"), failure}});
    root.insert(QStringLiteral("vendorStyleExtension"), QVariantMap{
        {QStringLiteral("accent"), QStringLiteral("#cd7853")},
        {QStringLiteral("binaryPayload"), QByteArray::fromHex("102030ff")}});
    root.insert(QStringLiteral("displayProjection"), QVariantMap{
        {QStringLiteral("authoredLabel"), QStringLiteral("front panel")},
        {QStringLiteral("thumbnailRetryState"), QVariantMap{
            {QStringLiteral("thumbnailQueueDepth"), source.size()},
            {QStringLiteral("attempt"), 3}}}});
    auto children = root.value(QStringLiteral("children")).toList();
    auto fill = children.first().toMap();
    fill.insert(QStringLiteral("ThUmBnAiLQueueToken"), failure);
    fill.insert(QStringLiteral("thumbnailFailureDetails"), QVariantList{source, failure});
    fill.insert(QStringLiteral("unknownAuthoredBinary"), QByteArray::fromHex("aabbccdd"));
    children[0] = fill;
    root.insert(QStringLiteral("children"), children);
    stack->first() = root;
  };
  add_view_state(&thumbnail_variant_a, QByteArray("view-state-a"), QStringLiteral("failure-a"));
  add_view_state(&thumbnail_variant_b, QByteArray("different-view-state-b"), QStringLiteral("failure-b"));
  const auto thumbnail_doc_a = document_from_layers(
      thumbnail_variant_a, masks, {QStringLiteral("component-group")}, 64, 48, &error);
  const auto thumbnail_doc_b = document_from_layers(
      thumbnail_variant_b, masks, {QStringLiteral("component-group")}, 64, 48, &error);
  const auto durable_layers_a = thumbnail_doc_a.value(QStringLiteral("layers"));
  const auto durable_layers_b = thumbnail_doc_b.value(QStringLiteral("layers"));
  const auto durable_root = durable_layers_a.toList().first().toMap();
  const auto durable_fill = durable_root.value(QStringLiteral("children")).toList().first().toMap();
  if (!check(!thumbnail_doc_a.isEmpty() && !thumbnail_doc_b.isEmpty() && error.isEmpty() &&
                 durable_layers_a == durable_layers_b &&
                 !contains_thumbnail_key(durable_layers_a) &&
                 durable_root.value(QStringLiteral("layerStyle")) ==
                     authored_stack.first().toMap().value(QStringLiteral("layerStyle")) &&
                 durable_root.value(QStringLiteral("vendorStyleExtension")).toMap()
                     .value(QStringLiteral("binaryPayload")).toByteArray() ==
                     QByteArray::fromHex("102030ff") &&
                 durable_root.value(QStringLiteral("displayProjection")).toMap()
                     .value(QStringLiteral("authoredLabel")).toString() ==
                     QStringLiteral("front panel") &&
                 durable_fill.value(QStringLiteral("unknownAuthoredBinary")).toByteArray() ==
                     QByteArray::fromHex("aabbccdd"),
             "Component capture must strip recursively transient thumbnail state while preserving every authored field")) return 1;

  const auto serialized = serialize_document(document, &error);
  const auto reopened = deserialize_document(serialized, &error);
  if (!check(!serialized.isEmpty() && !reopened.isEmpty() && error.isEmpty() &&
                 reopened.value(QStringLiteral("contentId")) == document.value(QStringLiteral("contentId")) &&
                 reopened.value(QStringLiteral("layers")).toList().first().toMap()
                     .value(QStringLiteral("children")).toList().at(3).toMap()
                     .value(QStringLiteral("pixelData")).toByteArray() ==
                     QByteArray::fromHex("ff0000ff00ff00ff0000ffffffffffff") &&
                 reopened.value(QStringLiteral("maskStack")).toList().first().toMap()
                     .value(QStringLiteral("maskData")).toByteArray() == QByteArray::fromHex("000102ff"),
             "Nested document save/reopen did not round-trip identity or binary layer/mask payloads")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  const auto painted_layer = painted_pixel_layer();
  const auto painted_tiles = painted_layer.value(QStringLiteral("paintTiles")).toMap();
  const auto painted_document = document_from_layers(
      {painted_layer}, {}, {QStringLiteral("painted-pixel")}, 2, 2, &error);
  const auto painted_document_layer = painted_document.value(QStringLiteral("layers")).toList()
                                         .first().toMap();
  const auto painted_bytes = serialize_document(painted_document, &error);
  const auto painted_reopened = deserialize_document(painted_bytes, &error);
  const auto reopened_paint_layer = painted_reopened.value(QStringLiteral("layers")).toList()
                                        .first().toMap();
  PhotoPaintSurface reopened_surface;
  const auto reopened_tiles = reopened_paint_layer.value(QStringLiteral("paintTiles")).toMap();
  const auto reopened_tile = [&]() {
    QString surface_error;
    if (!reopened_surface.load(reopened_paint_layer, &surface_error)) return QImage{};
    return reopened_surface.tile(0, 0, &surface_error);
  }();
  const auto* reopened_first_row = reopened_tile.isNull() ? nullptr :
      reinterpret_cast<const QRgbaFloat32*>(reopened_tile.constScanLine(0));
  if (!check(error.isEmpty() && !painted_document.isEmpty() && !painted_bytes.isEmpty() &&
                 painted_document_layer.value(QStringLiteral("paintTiles")).toMap() == painted_tiles &&
                 reopened_tiles == painted_tiles && !reopened_tile.isNull() &&
                 reopened_first_row != nullptr &&
                 std::abs(reopened_first_row[0].red() - 4.0F) < 0.0001F &&
                 std::abs(reopened_first_row[0].green() - 0.25F) < 0.0001F &&
                 std::abs(reopened_first_row[0].blue() + 0.1F) < 0.0001F &&
                 std::abs(reopened_first_row[0].alpha() - 0.5F) < 0.0001F,
             "Committed editable paint tiles did not validate and round-trip exactly with HDR premultiplied pixels")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto painted_component = create_component(
      {painted_layer}, {}, {QStringLiteral("painted-pixel")}, 2, 2, &error);
  const auto painted_instance = layer_by_id(painted_component, QStringLiteral("painted-pixel"));
  const auto converted_paint_tiles = painted_instance.value(QStringLiteral("smartObjectDocument"))
                                         .toMap().value(QStringLiteral("layers")).toList()
                                         .first().toMap().value(QStringLiteral("paintTiles")).toMap();
  if (!check(error.isEmpty() && !painted_instance.isEmpty() &&
                 converted_paint_tiles == painted_tiles,
             "Converting a committed painted pixel layer did not retain its editable tile data")) return 1;

  const auto rejects_paint_layer = [&](const QVariantMap& layer,
                                       const char* message) {
    QString paint_error;
    const auto result = document_from_layers(
        {layer}, {}, {layer.value(QStringLiteral("id")).toString()}, 2, 2,
        &paint_error);
    return check(result.isEmpty() && !paint_error.isEmpty(), message);
  };
  auto malformed_tiles_layer = painted_layer;
  auto malformed_tiles = malformed_tiles_layer.value(QStringLiteral("paintTiles")).toMap();
  malformed_tiles.insert(QStringLiteral("format"), QStringLiteral("rgba-u8-v0"));
  malformed_tiles_layer.insert(QStringLiteral("paintTiles"), malformed_tiles);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Unsupported committed paint tile formats were accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles = malformed_tiles_layer.value(QStringLiteral("paintTiles")).toMap();
  malformed_tiles.insert(QStringLiteral("icc"), QStringLiteral("not-a-profile"));
  malformed_tiles_layer.insert(QStringLiteral("paintTiles"), malformed_tiles);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Invalid committed paint tile ICC profiles were accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles = malformed_tiles_layer.value(QStringLiteral("paintTiles")).toMap();
  malformed_tiles.insert(QStringLiteral("icc"), QString::fromLatin1(QByteArray("not-an-icc").toBase64()));
  malformed_tiles_layer.insert(QStringLiteral("paintTiles"), malformed_tiles);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Base64 data that is not a valid ICC profile was accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles = malformed_tiles_layer.value(QStringLiteral("paintTiles")).toMap();
  auto encoded_tiles = malformed_tiles.value(QStringLiteral("tiles")).toMap();
  encoded_tiles.insert(QStringLiteral("0,0"), QStringLiteral("AAAAAA=="));
  malformed_tiles.insert(QStringLiteral("tiles"), encoded_tiles);
  malformed_tiles_layer.insert(QStringLiteral("paintTiles"), malformed_tiles);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Truncated committed paint tile data was accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles = malformed_tiles_layer.value(QStringLiteral("paintTiles")).toMap();
  encoded_tiles = malformed_tiles.value(QStringLiteral("tiles")).toMap();
  encoded_tiles.insert(QStringLiteral("0,0"), QString::fromLatin1(
      QByteArray::fromHex("ffffffff01").toBase64()));
  malformed_tiles.insert(QStringLiteral("tiles"), encoded_tiles);
  malformed_tiles_layer.insert(QStringLiteral("paintTiles"), malformed_tiles);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "A compressed paint tile with an excessive declared output size was accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("pixelWidth"), 8192);
  malformed_tiles_layer.insert(QStringLiteral("pixelHeight"), 8192);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Paint tile canvas dimensions exceeding 64 MP were accepted")) return 1;

  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("paintDraftTiles"), QVariantMap{});
  if (!rejects_paint_layer(malformed_tiles_layer,
          "An uncommitted paint draft was accepted in a Smart Object document")) return 1;
  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("paintDraftImage"), QByteArray("draft"));
  if (!rejects_paint_layer(malformed_tiles_layer,
          "An uncommitted draft image was accepted in a Smart Object document")) return 1;
  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("baseImageEdit"), true);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "A base-image edit dependency was accepted in an isolated Smart Object")) return 1;
  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("copyBaseImage"), true);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "A copied base-image dependency was accepted in an isolated Smart Object")) return 1;
  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("copyBaseImageThroughAdjustments"), true);
  if (!rejects_paint_layer(malformed_tiles_layer,
          "A copied base-image dependency was accepted in an isolated Smart Object")) return 1;
  malformed_tiles_layer = painted_layer;
  malformed_tiles_layer.insert(QStringLiteral("kind"), QStringLiteral("fill"));
  if (!rejects_paint_layer(malformed_tiles_layer,
          "Committed paintTiles were accepted on a non-pixel layer")) return 1;

  const auto rejects_invalid_paint_pixel = [&](const QRgbaFloat32& pixel,
                                               const char* message) {
    auto invalid_layer = painted_pixel_layer(QStringLiteral("invalid-pixel"));
    auto invalid_payload = invalid_layer.value(QStringLiteral("paintTiles")).toMap();
    auto invalid_encoded_tiles = invalid_payload.value(QStringLiteral("tiles")).toMap();
    QImage invalid_tile(QSize(2, 2), QImage::Format_RGBA32FPx4_Premultiplied);
    invalid_tile.fill(Qt::transparent);
    auto* row = reinterpret_cast<QRgbaFloat32*>(invalid_tile.scanLine(0));
    row[0] = pixel;
    invalid_encoded_tiles.insert(QStringLiteral("0,0"), PhotoPaintSurface::encode_tile(invalid_tile));
    invalid_payload.insert(QStringLiteral("tiles"), invalid_encoded_tiles);
    invalid_layer.insert(QStringLiteral("paintTiles"), invalid_payload);
    return rejects_paint_layer(invalid_layer, message);
  };
  if (!rejects_invalid_paint_pixel(
          QRgbaFloat32{0.1F, 0.0F, 0.0F, 0.0F},
          "Non-premultiplied zero-alpha paint pixels were accepted") ||
      !rejects_invalid_paint_pixel(
          QRgbaFloat32{0.1F, 0.0F, 0.0F, 1.5F},
          "Paint tiles with alpha above one were accepted") ||
      !rejects_invalid_paint_pixel(
          QRgbaFloat32{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F},
          "Paint tiles with a non-finite channel were accepted")) return 1;

  auto invalid_coordinates_layer = painted_layer;
  auto invalid_coordinates_payload = invalid_coordinates_layer.value(QStringLiteral("paintTiles")).toMap();
  invalid_coordinates_payload.insert(QStringLiteral("tiles"), QVariantMap{
      {QStringLiteral("1,0"), painted_tiles.value(QStringLiteral("tiles")).toMap()
           .value(QStringLiteral("0,0"))}});
  invalid_coordinates_layer.insert(QStringLiteral("paintTiles"), invalid_coordinates_payload);
  if (!rejects_paint_layer(invalid_coordinates_layer,
          "Committed paint tiles outside the canvas were accepted")) return 1;

  const auto converted = create_component(input_stack, masks,
      {QStringLiteral("component-group")}, 64, 48, &error);
  const auto instance = layer_by_id(converted, QStringLiteral("component-group"));
  const auto converted_document = instance.value(QStringLiteral("smartObjectDocument")).toMap();
  if (!check(error.isEmpty() && !instance.isEmpty() &&
                 instance.value(QStringLiteral("kind")).toString() == QStringLiteral("group") &&
                 instance.value(QStringLiteral("pixelWidth")).toInt() == 64 &&
                 instance.value(QStringLiteral("pixelHeight")).toInt() == 48 &&
                 instance.value(QStringLiteral("canvasWidth")).toInt() == 64 &&
                 instance.value(QStringLiteral("canvasHeight")).toInt() == 48 &&
                 instance.value(QStringLiteral("x")).toDouble() == 0.0 &&
                 instance.value(QStringLiteral("opacity")).toDouble() == 1.0 &&
                 instance.value(QStringLiteral("blendMode")).toString() == QStringLiteral("normal") &&
                 instance.value(QStringLiteral("children")).toList().isEmpty() &&
                 instance.value(QStringLiteral("smartObjectInstanceId")).toString() != QString() &&
                 converted_document.value(QStringLiteral("layers")).toList() == document.value(QStringLiteral("layers")).toList() &&
                 converted.size() == input_stack.size() &&
                 converted.at(1).toMap() == input_stack.at(1).toMap(),
             "Conversion did not create a neutral full-canvas instance and preserve the original subtree/siblings")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  const auto flat_stack = flat_group_stack();
  const auto flat_document = document_from_layers(
      flat_stack, masks, {QStringLiteral("flat-component-group")}, 64, 48, &error);
  const auto flat_document_layers = flat_document.value(QStringLiteral("layers")).toList();
  if (!check(error.isEmpty() && flat_document_layers.size() == 4 &&
                 layer_by_id(flat_document_layers, QStringLiteral("flat-fill")).size() > 0 &&
                 layer_by_id(flat_document_layers, QStringLiteral("flat-text"))
                     .value(QStringLiteral("text")).toString() == QStringLiteral("Flat editable title") &&
                 layer_by_id(flat_document_layers, QStringLiteral("flat-image"))
                     .value(QStringLiteral("smartObject")).toMap()
                     .value(QStringLiteral("policy")).toString() == QStringLiteral("embedded") &&
                 layer_by_id(flat_document_layers, QStringLiteral("flat-unselected-sibling")).isEmpty(),
             "Flat parentId group conversion did not capture exactly its descendant content")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }
  const auto flat_converted = create_component(
      flat_stack, masks, {QStringLiteral("flat-component-group")}, 64, 48, &error);
  const auto flat_instance = layer_by_id(flat_converted,
                                         QStringLiteral("flat-component-group"));
  const auto flat_result_doc = flat_instance.value(QStringLiteral("smartObjectDocument")).toMap();
  if (!check(error.isEmpty() && flat_converted.size() == 2 &&
                 flat_instance.value(QStringLiteral("kind")).toString() == QStringLiteral("group") &&
                 flat_result_doc.value(QStringLiteral("layers")).toList().size() == 4 &&
                 layer_by_id(flat_converted, QStringLiteral("flat-fill")).isEmpty() &&
                 layer_by_id(flat_converted, QStringLiteral("flat-text")).isEmpty() &&
                 layer_by_id(flat_converted, QStringLiteral("flat-image")).isEmpty() &&
                 layer_by_id(flat_converted, QStringLiteral("flat-unselected-sibling")).isEmpty() == false &&
                 flat_converted.at(1).toMap().value(QStringLiteral("id")).toString() ==
                     QStringLiteral("flat-unselected-sibling"),
             "Flat group conversion left orphaned descendants or removed an unselected sibling")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  const QVariantMap locked_flat_parent{
      {QStringLiteral("id"), QStringLiteral("locked-flat-parent")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("locked"), true}};
  auto nested_component = component_instance(QStringLiteral("locked-flat-child"), document, 12.0);
  nested_component.insert(QStringLiteral("parentId"), QStringLiteral("locked-flat-parent"));
  const QVariantList flat_locked_instances{locked_flat_parent, nested_component};
  const auto flat_lock_result = make_independent(
      flat_locked_instances, {QStringLiteral("locked-flat-child")}, &error);
  if (!check(!error.isEmpty() && flat_lock_result == flat_locked_instances &&
                 error.contains(QStringLiteral("parent group is locked")),
             "A locked flat parentId group did not block a component mutation")) return 1;

  const auto group_content_id = document.value(QStringLiteral("contentId")).toString();
  const auto duplicate = duplicate_instance(instance, QStringLiteral("component-duplicate"), &error);
  if (!check(error.isEmpty() && duplicate.value(QStringLiteral("id")).toString() ==
                 QStringLiteral("component-duplicate") &&
                 duplicate.value(QStringLiteral("smartObjectInstanceId")).toString() !=
                     instance.value(QStringLiteral("smartObjectInstanceId")).toString() &&
                 duplicate.value(QStringLiteral("smartObjectDocument")).toMap()
                     .value(QStringLiteral("contentId")).toString() ==
                     instance.value(QStringLiteral("smartObjectDocument")).toMap()
                         .value(QStringLiteral("contentId")).toString() &&
                 duplicate.value(QStringLiteral("smartObjectDocument")).toMap()
                     .value(QStringLiteral("revision")).toInt() == 1 &&
                 without_document_and_identity(duplicate) == without_document_and_identity(instance),
             "Duplicating a component did not share content while keeping new instance identity and placement")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  auto shared_document = document;
  shared_document.insert(QStringLiteral("revision"), 2);
  const auto shared_a = component_instance(QStringLiteral("instance-a"), shared_document, 12.0);
  const auto shared_b = component_instance(QStringLiteral("instance-b"), shared_document, 84.0);
  const QVariantList shared_stack{shared_a, shared_b};
  const auto independent = make_independent(shared_stack, {QStringLiteral("instance-b")}, &error);
  const auto independent_a = layer_by_id(independent, QStringLiteral("instance-a"));
  const auto independent_b = layer_by_id(independent, QStringLiteral("instance-b"));
  const auto independent_doc = independent_b.value(QStringLiteral("smartObjectDocument")).toMap();
  if (!check(error.isEmpty() &&
                 independent_a.value(QStringLiteral("smartObjectDocument")).toMap()
                     .value(QStringLiteral("contentId")).toString() == group_content_id &&
                 independent_doc.value(QStringLiteral("contentId")).toString() != group_content_id &&
                 independent_doc.value(QStringLiteral("revision")).toInt() == 2 &&
                 independent_b.value(QStringLiteral("smartObjectInstanceId")) == shared_b.value(QStringLiteral("smartObjectInstanceId")) &&
                 without_document_and_identity(independent_b) == without_document_and_identity(shared_b),
             "Make Independent changed an unselected sibling or the selected instance placement/identity")) return 1;

  auto replacement_content = document;
  replacement_content.insert(QStringLiteral("contentId"), new_id());
  replacement_content.insert(QStringLiteral("revision"), 4);
  auto replacement_layers = replacement_content.value(QStringLiteral("layers")).toList();
  auto replacement_group = replacement_layers.first().toMap();
  auto replacement_children = replacement_group.value(QStringLiteral("children")).toList();
  auto replacement_text = replacement_children.at(1).toMap();
  replacement_text.insert(QStringLiteral("text"), QStringLiteral("Replacement title"));
  replacement_children[1] = replacement_text;
  replacement_group.insert(QStringLiteral("children"), replacement_children);
  replacement_layers[0] = replacement_group;
  replacement_content.insert(QStringLiteral("layers"), replacement_layers);
  const auto replacement_stack = replace_shared_document(
      shared_stack, {QStringLiteral("instance-a")}, replacement_content, &error);
  const auto replaced_a = layer_by_id(replacement_stack, QStringLiteral("instance-a"));
  const auto replaced_b = layer_by_id(replacement_stack, QStringLiteral("instance-b"));
  const auto replaced_doc = replaced_a.value(QStringLiteral("smartObjectDocument")).toMap();
  if (!check(error.isEmpty() &&
                 replaced_doc.value(QStringLiteral("contentId")).toString() == group_content_id &&
                 replaced_doc.value(QStringLiteral("revision")).toInt() == 5 &&
                 replaced_doc == replaced_b.value(QStringLiteral("smartObjectDocument")).toMap() &&
                 replaced_a.value(QStringLiteral("id")).toString() == shared_a.value(QStringLiteral("id")).toString() &&
                 replaced_a.value(QStringLiteral("smartObjectInstanceId")).toString() ==
                     shared_a.value(QStringLiteral("smartObjectInstanceId")).toString() &&
                 replaced_b.value(QStringLiteral("smartObjectInstanceId")).toString() ==
                     shared_b.value(QStringLiteral("smartObjectInstanceId")).toString() &&
                 layer_by_id(replaced_doc.value(QStringLiteral("layers")).toList(),
                             QStringLiteral("component-title")).value(QStringLiteral("text")).toString() ==
                     QStringLiteral("Replacement title") &&
                 without_document_and_identity(replaced_a) == without_document_and_identity(shared_a) &&
                 without_document_and_identity(replaced_b) == without_document_and_identity(shared_b),
             "Replace Contents did not revise every shared instance atomically while preserving outer placement")) return 1;

  auto edited_document = shared_document;
  edited_document.insert(QStringLiteral("revision"), 2);
  const auto edited_title = layer_by_id(edited_document.value(QStringLiteral("layers")).toList(),
                                        QStringLiteral("component-title"));
  auto title_update = edited_title;
  title_update.insert(QStringLiteral("text"), QStringLiteral("Edited shared title"));
  title_update.insert(QStringLiteral("authoredTextExtension"), QVariantMap{
      {QStringLiteral("trackingName"), QStringLiteral("campaign-headline")},
      {QStringLiteral("binaryData"), QByteArray::fromHex("a1b2c3d4")}});
  title_update.insert(QStringLiteral("thumbnailQueuePreview"), QByteArray("transient"));
  title_update.insert(QStringLiteral("authoredProjection"), QVariantMap{
      {QStringLiteral("fontRole"), QStringLiteral("display")},
      {QStringLiteral("thumbnailFailureGeneration"), 12}});
  edited_document = replace_in_document(edited_document,
                                        QStringLiteral("component-title"),
                                        title_update);
  const auto applied_stack = apply_shared_document(
      shared_stack, {QStringLiteral("instance-a")}, edited_document, 2, &error);
  const auto applied_a = layer_by_id(applied_stack, QStringLiteral("instance-a"));
  const auto applied_b = layer_by_id(applied_stack, QStringLiteral("instance-b"));
  if (!check(error.isEmpty() &&
                 applied_a.value(QStringLiteral("smartObjectDocument")).toMap() ==
                     applied_b.value(QStringLiteral("smartObjectDocument")).toMap() &&
                 applied_a.value(QStringLiteral("smartObjectInstanceId")).toString() ==
                     shared_a.value(QStringLiteral("smartObjectInstanceId")).toString() &&
                 applied_b.value(QStringLiteral("smartObjectInstanceId")).toString() ==
                     shared_b.value(QStringLiteral("smartObjectInstanceId")).toString() &&
                 applied_a.value(QStringLiteral("smartObjectDocument")).toMap()
                     .value(QStringLiteral("revision")).toInt() == 3 &&
                 layer_by_id(applied_a.value(QStringLiteral("smartObjectDocument")).toMap()
                                 .value(QStringLiteral("layers")).toList(),
                             QStringLiteral("component-title")).value(QStringLiteral("text")).toString() ==
                     QStringLiteral("Edited shared title") &&
                 !contains_thumbnail_key(applied_a.value(QStringLiteral("smartObjectDocument"))) &&
                 layer_by_id(applied_a.value(QStringLiteral("smartObjectDocument")).toMap()
                                 .value(QStringLiteral("layers")).toList(),
                             QStringLiteral("component-title")).value(QStringLiteral("authoredTextExtension")).toMap()
                     .value(QStringLiteral("binaryData")).toByteArray() == QByteArray::fromHex("a1b2c3d4") &&
                 layer_by_id(applied_a.value(QStringLiteral("smartObjectDocument")).toMap()
                                 .value(QStringLiteral("layers")).toList(),
                             QStringLiteral("component-title")).value(QStringLiteral("authoredProjection")).toMap()
                     .value(QStringLiteral("fontRole")).toString() == QStringLiteral("display") &&
                 without_document_and_identity(applied_a) == without_document_and_identity(shared_a) &&
                 without_document_and_identity(applied_b) == without_document_and_identity(shared_b),
             "Applying a nested edit did not update all shared copies with one new revision")) return 1;
  const auto stale_edit = apply_shared_document(
      shared_stack, {QStringLiteral("instance-a")}, edited_document, 1, &error);
  if (!check(!error.isEmpty() && stale_edit == shared_stack,
             "A stale nested editor revision was allowed to replace accepted content")) return 1;

  auto conflicting_content = shared_document;
  conflicting_content.insert(QStringLiteral("revision"), 7);
  auto conflicting_title = layer_by_id(conflicting_content.value(QStringLiteral("layers")).toList(),
                                       QStringLiteral("component-title"));
  conflicting_title.insert(QStringLiteral("text"), QStringLiteral("Conflicting shared copy"));
  conflicting_content = replace_in_document(conflicting_content,
                                             QStringLiteral("component-title"),
                                             conflicting_title);
  const auto conflicting_stack = QVariantList{
      shared_a, component_instance(QStringLiteral("instance-conflict"), conflicting_content, 90.0)};
  const auto conflict_result = replace_shared_document(
      conflicting_stack, {QStringLiteral("instance-a")}, replacement_content, &error);
  if (!check(!error.isEmpty() && conflict_result == conflicting_stack &&
                 error.contains(QStringLiteral("conflicting Smart Object documents")),
             "Divergent documents with one shared contentId were not rejected atomically")) return 1;

  auto locked_sibling = shared_b;
  locked_sibling.insert(QStringLiteral("locked"), true);
  const QVariantList locked_sibling_stack{shared_a, locked_sibling};
  const auto locked_replace = replace_shared_document(
      locked_sibling_stack, {QStringLiteral("instance-a")}, replacement_content, &error);
  if (!check(!error.isEmpty() && locked_replace == locked_sibling_stack,
             "Shared replacement was not rejected atomically when a sibling was locked")) return 1;

  auto parent_a = shared_a;
  auto parent_b = shared_b;
  parent_a.insert(QStringLiteral("parentId"), QStringLiteral("locked-parent"));
  parent_b.insert(QStringLiteral("parentId"), QStringLiteral("locked-parent"));
  const QVariantList locked_parent_stack{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("locked-parent")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("locked"), true},
      {QStringLiteral("children"), QVariantList{parent_a, parent_b}}}};
  const auto locked_parent_result = apply_shared_document(
      locked_parent_stack, {QStringLiteral("instance-a")}, edited_document, 2, &error);
  if (!check(!error.isEmpty() && locked_parent_result == locked_parent_stack,
             "Shared edits were not rejected atomically below a locked parent group")) return 1;

  auto invalid_mask_doc = document;
  auto invalid_mask_layers = invalid_mask_doc.value(QStringLiteral("layers")).toList();
  auto invalid_group = invalid_mask_layers.first().toMap();
  auto invalid_children = invalid_group.value(QStringLiteral("children")).toList();
  auto invalid_fill = invalid_children.first().toMap();
  invalid_fill.insert(QStringLiteral("selectionMaskSetId"), QStringLiteral("missing-mask"));
  invalid_children[0] = invalid_fill;
  invalid_group.insert(QStringLiteral("children"), invalid_children);
  invalid_mask_layers[0] = invalid_group;
  invalid_mask_doc.insert(QStringLiteral("layers"), invalid_mask_layers);
  if (!check(!validate_document(invalid_mask_doc, &error) &&
                 error.contains(QStringLiteral("mask reference")),
             "An external mask reference was not rejected")) return 1;

  const QVariantList validator_layers{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("retouch-validator-fill")},
      {QStringLiteral("kind"), QStringLiteral("fill")},
      {QStringLiteral("fillColor"), QStringLiteral("#8090a0")}}};
  for (const auto& mode : {QStringLiteral("remove"), QStringLiteral("heal"),
                           QStringLiteral("patch"), QStringLiteral("clone")}) {
    const auto retouch = component_retouch_mask(QStringLiteral("component-retouch-a"), mode);
    const auto component_doc = make_document(new_id(), validator_layers, 1, {retouch});
    if (!check(validate_document(component_doc, &error),
               "A supported component retouch mask without a selection set id was rejected")) {
      std::cerr << error.toStdString() << '\n';
      return 1;
    }
    const auto encoded = serialize_document(component_doc, &error);
    const auto reparsed = deserialize_document(encoded, &error);
    const auto reparsed_masks = reparsed.value(QStringLiteral("maskStack")).toList();
    if (!check(!encoded.isEmpty() && reparsed_masks.size() == 1,
               "A valid component retouch document failed its serialization round trip")) {
      std::cerr << error.toStdString() << '\n';
      return 1;
    }
    const auto persisted_mask = reparsed_masks.first().toMap();
    if (!check(persisted_mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1 &&
                   persisted_mask.value(QStringLiteral("repairId")).toString() == QStringLiteral("component-retouch-a") &&
                   persisted_mask.value(QStringLiteral("maskSetId")).toString().isEmpty(),
               "A valid component retouch marker did not survive document serialization without becoming a selection set")) {
      std::cerr << error.toStdString() << '\n';
      return 1;
    }
  }
  auto boundary_component_basis = component_retouch_mask(
      QStringLiteral("boundary-component-basis"), QStringLiteral("clone"));
  boundary_component_basis.insert(QStringLiteral("componentRetouchCanvasWidth"), 8000);
  boundary_component_basis.insert(QStringLiteral("componentRetouchCanvasHeight"), 8000);
  if (!check(validate_document(
                 make_document(new_id(), validator_layers, 1, {boundary_component_basis}), &error),
             "A component repair's original 64 MP source basis was rejected after document resizing")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  auto regular_empty_set_retouch = component_retouch_mask(
      QStringLiteral("ordinary-retouch"), QStringLiteral("remove"));
  regular_empty_set_retouch.remove(QStringLiteral("componentPostCompositeRetouchVersion"));
  regular_empty_set_retouch.remove(QStringLiteral("componentRetouchCanvasWidth"));
  regular_empty_set_retouch.remove(QStringLiteral("componentRetouchCanvasHeight"));
  auto invalid_component_version = component_retouch_mask(
      QStringLiteral("invalid-component-version"), QStringLiteral("clone"));
  invalid_component_version.insert(QStringLiteral("componentPostCompositeRetouchVersion"), 2);
  auto invalid_component_mode = component_retouch_mask(
      QStringLiteral("invalid-component-mode"), QStringLiteral("dust"));
  auto invalid_component_basis = component_retouch_mask(
      QStringLiteral("invalid-component-basis"), QStringLiteral("heal"));
  invalid_component_basis.insert(QStringLiteral("componentRetouchCanvasWidth"), 8192);
  invalid_component_basis.insert(QStringLiteral("componentRetouchCanvasHeight"), 8192);
  auto missing_component_repair_id = component_retouch_mask(
      QStringLiteral("missing-component-repair-id"), QStringLiteral("patch"));
  missing_component_repair_id.remove(QStringLiteral("repairId"));
  auto empty_component_repair_id = component_retouch_mask(
      QStringLiteral("empty-component-repair-id"), QStringLiteral("patch"));
  empty_component_repair_id.insert(QStringLiteral("repairId"), QStringLiteral("  "));
  auto missing_selection_set_id = mask_definition();
  missing_selection_set_id.remove(QStringLiteral("maskSetId"));
  for (const auto& malformed : {regular_empty_set_retouch, invalid_component_version,
                                invalid_component_mode, invalid_component_basis,
                                missing_component_repair_id, empty_component_repair_id,
                                missing_selection_set_id}) {
    if (!check(!validate_document(make_document(new_id(), validator_layers, 1, {malformed}), &error),
               "An ordinary retouch or malformed component marker bypassed mask identity validation")) {
      std::cerr << error.toStdString() << '\n';
      return 1;
    }
  }

  auto retouch_reference_layer = validator_layers.first().toMap();
  retouch_reference_layer.insert(QStringLiteral("selectionMaskSetId"),
                                 QStringLiteral("component-retouch-reference"));
  retouch_reference_layer.insert(QStringLiteral("selectionMaskSetName"),
                                 QStringLiteral("Retouch operation"));
  auto retouch_reference_mask = component_retouch_mask(
      QStringLiteral("component-retouch-reference"), QStringLiteral("clone"));
  retouch_reference_mask.insert(QStringLiteral("maskSetId"),
                                QStringLiteral("component-retouch-reference"));
  retouch_reference_mask.insert(QStringLiteral("maskSetName"),
                                QStringLiteral("Retouch operation"));
  const auto retouch_reference_doc = make_document(
      new_id(), {retouch_reference_layer}, 1,
      {retouch_reference_mask});
  if (!check(!validate_document(retouch_reference_doc, &error) &&
                 error.contains(QStringLiteral("mask reference")),
             "A retouch operation id was incorrectly accepted as a child selection-mask reference")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  auto duplicate_component_id = component_retouch_mask(
      QStringLiteral("duplicate-component-mask"), QStringLiteral("clone"));
  auto duplicate_selection_id = mask_definition();
  duplicate_selection_id.insert(QStringLiteral("id"), QStringLiteral("duplicate-component-mask"));
  if (!check(!validate_document(make_document(new_id(), validator_layers, 1,
                                             {duplicate_component_id, duplicate_selection_id}), &error) &&
                 error.contains(QStringLiteral("unique IDs")),
             "Component repair ids were not kept unique across the embedded mask stack")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  auto unresolved_doc = document;
  auto unresolved_layers = unresolved_doc.value(QStringLiteral("layers")).toList();
  auto unresolved_group = unresolved_layers.first().toMap();
  unresolved_group.insert(QStringLiteral("smartDocumentId"), QStringLiteral("object-table-ref"));
  unresolved_layers[0] = unresolved_group;
  unresolved_doc.insert(QStringLiteral("layers"), unresolved_layers);
  if (!check(!validate_document(unresolved_doc, &error) &&
                 error.contains(QStringLiteral("unresolved nested-document")),
             "An unsupported external nested-document reference was not rejected")) return 1;

  auto nested_doc = make_document(new_id(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("nested-fill")},
                   {QStringLiteral("kind"), QStringLiteral("fill")},
                   {QStringLiteral("fillColor"), QStringLiteral("#a0b0c0")}}});
  auto cyclic_doc = make_document(nested_doc.value(QStringLiteral("contentId")).toString(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("nested-instance")},
                   {QStringLiteral("kind"), QStringLiteral("group")},
                   {QStringLiteral("smartObjectInstanceId"), new_id()},
                   {QStringLiteral("smartObjectDocument"), nested_doc}}});
  if (!check(!validate_document(cyclic_doc, &error) &&
                 error.contains(QStringLiteral("cycle")),
             "A repeated nested contentId cycle was not rejected")) return 1;

  const QVariantList parent_cycle{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("parent-cycle-a")},
                  {QStringLiteral("parentId"), QStringLiteral("parent-cycle-b")},
                  {QStringLiteral("kind"), QStringLiteral("fill")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("parent-cycle-b")},
                  {QStringLiteral("parentId"), QStringLiteral("parent-cycle-a")},
                  {QStringLiteral("kind"), QStringLiteral("fill")}}};
  if (!check(!validate_document(make_document(new_id(), parent_cycle), &error) &&
                 error.contains(QStringLiteral("parent hierarchy contains a cycle")),
             "A cyclic child-layer parent hierarchy was not rejected")) return 1;

  auto deep_doc = make_document(new_id(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("depth-leaf")},
                   {QStringLiteral("kind"), QStringLiteral("fill")}}});
  for (int index = 0; index < kMaximumDocumentDepth; ++index) {
    deep_doc = make_document(new_id(),
        {QVariantMap{{QStringLiteral("id"), QStringLiteral("depth-%1").arg(index)},
                     {QStringLiteral("kind"), QStringLiteral("group")},
                     {QStringLiteral("smartObjectInstanceId"), new_id()},
                     {QStringLiteral("smartObjectDocument"), deep_doc}}});
  }
  if (!check(!validate_document(deep_doc, &error) &&
                 error.contains(QStringLiteral("8-document")),
             "Nested Smart Object recursion was not bounded")) return 1;

  QVariantList too_many_layers;
  too_many_layers.reserve(kMaximumLayerCount + 1);
  for (int index = 0; index <= kMaximumLayerCount; ++index) {
    too_many_layers.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("layer-%1").arg(index)},
        {QStringLiteral("kind"), QStringLiteral("fill")}});
  }
  const auto oversized_count = make_document(new_id(), too_many_layers);
  if (!check(!validate_document(oversized_count, &error) &&
                 error.contains(QStringLiteral("4096-layer")),
             "Nested Smart Object layer count was not bounded")) return 1;

  auto oversized_bytes = make_document(new_id(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("large-payload")},
                   {QStringLiteral("kind"), QStringLiteral("fill")},
                   {QStringLiteral("opaquePayload"), QByteArray(
                       static_cast<qsizetype>(kMaximumSerializedBytes * 3 / 4 + 1024), 'x')}}});
  if (!check(!validate_document(oversized_bytes, &error) &&
                 error.contains(QStringLiteral("64 MiB")),
             "Nested Smart Object serialized byte size was not bounded")) return 1;

  auto adjustment_only = make_document(new_id(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("adjustment-only")},
                   {QStringLiteral("kind"), QStringLiteral("adjustment")}}});
  const auto adjustment_stack = create_component(
      adjustment_only.value(QStringLiteral("layers")).toList(), {},
      {QStringLiteral("adjustment-only")}, 64, 48, &error);
  if (!check(!error.isEmpty() &&
                 adjustment_stack == adjustment_only.value(QStringLiteral("layers")).toList(),
             "Adjustment-only components were accepted without their required backdrop")) return 1;

  auto child_with_parent = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("nested-child")},
      {QStringLiteral("parentId"), QStringLiteral("external-parent")},
      {QStringLiteral("kind"), QStringLiteral("fill")}};
  const auto external_parent_doc = document_from_layers(
      {child_with_parent}, {}, {QStringLiteral("nested-child")}, 64, 48, &error);
  if (!check(external_parent_doc.isEmpty() &&
                 error.contains(QStringLiteral("parent that is missing from the layer stack")),
             "A layer with an unresolved external parent was copied into the nested document")) return 1;

  const QVariantMap external_source{
      {QStringLiteral("id"), QStringLiteral("external-image")},
      {QStringLiteral("kind"), QStringLiteral("source")},
      {QStringLiteral("sourcePath"), QStringLiteral("/outside/component/product.jpg")}};
  const auto external_source_doc = document_from_layers(
      {external_source}, {}, {QStringLiteral("external-image")}, 64, 48, &error);
  if (!check(external_source_doc.isEmpty() &&
                 error.contains(QStringLiteral("external sourcePath")),
             "A linked source path was accepted without an embedded editable original")) return 1;

  const QVariantList external_clipping_stack{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("clip-base")},
                  {QStringLiteral("kind"), QStringLiteral("fill")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("clipped-child")},
                  {QStringLiteral("kind"), QStringLiteral("fill")},
                  {QStringLiteral("clippingMask"), true}}};
  const auto external_clip_doc = document_from_layers(
      external_clipping_stack, {}, {QStringLiteral("clipped-child")}, 64, 48, &error);
  if (!check(external_clip_doc.isEmpty() &&
                 error.contains(QStringLiteral("clipping mask whose base layer is outside")),
             "A clipping dependency on an unselected sibling was not rejected")) return 1;

  const QVariantMap nested_layer{
      {QStringLiteral("id"), QStringLiteral("nested-child-with-parent")},
      {QStringLiteral("parentId"), QStringLiteral("external-group")},
      {QStringLiteral("kind"), QStringLiteral("fill")}};
  const auto nested_selection_stack = QVariantList{nested_layer};
  const auto nested_selection = create_component(
      nested_selection_stack, {}, {QStringLiteral("nested-child-with-parent")}, 64, 48, &error);
  if (!check(nested_selection == nested_selection_stack &&
                 error.contains(QStringLiteral("missing from the layer stack")),
             "Converting a child below an external group was not rejected atomically")) return 1;

  const auto oversized_canvas = document_from_layers(
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("canvas-fill")},
                   {QStringLiteral("kind"), QStringLiteral("fill")}}},
      {}, {QStringLiteral("canvas-fill")}, 9000, 9000, &error);
  if (!check(oversized_canvas.isEmpty() && error.contains(QStringLiteral("64 MP")),
             "Nested document canvas exceeded the first-slice pixel bound")) return 1;

  std::cout << "Nested Smart Object DTO serialization, editable hierarchy, shared revisions, identity, locks, references, recursion, count and byte limits passed\n";
  return 0;
}
