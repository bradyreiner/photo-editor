#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop::smart_object::nested_document {

// The v1 DTO is stored inline in each component instance as
// `smartObjectDocument`. Shared operations keep these copies identical by
// contentId and revision. Child layer maps use the existing editable layer
// schema, and maskStack keeps their selection references resolvable.
inline constexpr auto kSchema = "looksy.smart-object.document";
inline constexpr int kVersion = 1;
// Version 2 carries an independently scoped document-wide Layer Styles light.
inline constexpr int kDocumentLayerStyleLightVersion = 2;
// Version 3 negotiates editable dimensional text in nested component content.
inline constexpr int kTextDimensionVersion = 3;
inline constexpr int kMaximumDocumentDepth = 8;
inline constexpr int kMaximumLayerDepth = 64;
inline constexpr int kMaximumLayerCount = 4096;
inline constexpr int kMaximumMaskCount = 8192;
inline constexpr int kMaximumCanvasDimension = 100000;
inline constexpr qint64 kMaximumCanvasPixels = 64LL * 1000LL * 1000LL;
// Tile validation is cumulative over the fully expanded inline document tree.
inline constexpr qint64 kMaximumDecodedPaintPixels = 64LL * 1000LL * 1000LL;
inline constexpr qsizetype kMaximumSerializedBytes = 64LL * 1024LL * 1024LL;

// Remove view-only thumbnail queue state recursively while retaining unknown
// authored fields and committed content payloads. Useful at component editor
// ingress boundaries where layers came from a decorated LayerSession.
[[nodiscard]] QVariantList strip_transient_layer_view_fields(
    const QVariantList& layers);

// Returns a strict, versioned full-canvas document containing the selected
// subtree. The complete bounded mask stack is retained for existing layer
// mask references. Selected ancestors are preserved as wrappers when only a
// descendant is selected. `contentId` is generated and revision starts at 1.
[[nodiscard]] QVariantMap document_from_layers(
    const QVariantList& layers,
    const QVariantList& mask_stack,
    const QStringList& selected_layer_ids,
    int canvas_width,
    int canvas_height,
    QString* error = nullptr,
    double document_layer_style_light_angle = -45.0);

[[nodiscard]] bool validate_document(
    const QVariantMap& document,
    QString* error = nullptr);

[[nodiscard]] QByteArray serialize_document(
    const QVariantMap& document,
    QString* error = nullptr);

[[nodiscard]] QVariantMap deserialize_document(
    const QByteArray& bytes,
    QString* error = nullptr);

// Convert one selected layer/group into a neutral, full-parent-canvas group
// instance. The original subtree, styles, pixel/text/vector data, mask stack,
// and layer IDs are held in the nested document. The returned stack is
// unchanged when the input is invalid or unsupported.
[[nodiscard]] QVariantList create_component(
    const QVariantList& layer_stack,
    const QVariantList& mask_stack,
    const QStringList& selected_layer_ids,
    int canvas_width,
    int canvas_height,
    QString* error = nullptr,
    double document_layer_style_light_angle = -45.0);

// Copy an existing component layer to a new layer/instance ID while retaining
// its shared contentId and revision and all placement/style/mask state.
[[nodiscard]] QVariantMap duplicate_instance(
    const QVariantMap& instance,
    const QString& new_layer_id,
    QString* error = nullptr);

// Each selected component receives a fresh contentId; unselected shared
// siblings and every outer instance field remain unchanged.
[[nodiscard]] QVariantList make_independent(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    QString* error = nullptr);

// Replace each selected shared content group atomically. The original shared
// contentId is retained and the new revision is greater than both revisions.
[[nodiscard]] QVariantList replace_shared_document(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QVariantMap& replacement_document,
    QString* error = nullptr);

// Apply an edited document to every instance in the selected shared groups.
// `expected_revision` is a compare-and-swap guard; outer instance identity,
// placement, masks and styles are unchanged.
[[nodiscard]] QVariantList apply_shared_document(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QVariantMap& edited_document,
    int expected_revision,
    QString* error = nullptr);

}  // namespace photo_editor::desktop::smart_object::nested_document
