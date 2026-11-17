#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop::smart_object {

enum class ContentPolicy { Embedded, Linked };

// Returns whether a source filename belongs to a format the nested component
// schema can store. This is a suffix classification only; callers that are
// about to publish source bytes must still run the normal source decoder.
[[nodiscard]] bool supports_nested_source_file_format(const QString& file_name);

// Version 1 stores the exact original source bytes. Embedded bytes are base64
// in the recipe; linked content records an absolute path and expected SHA-256.
// The contentId identifies a reusable logical object, while sourceSha256
// identifies the immutable byte revision currently held by that object.
// Creating a source-backed descriptor reads and hashes its file; use a worker
// thread for source layers. PNG pixel-layer descriptors use the supplied bytes.
[[nodiscard]] QVariantMap create_content_descriptor(
    const QVariantMap& source_layer,
    ContentPolicy policy,
    QString* error = nullptr);

// With require_linked_source_available=false this is structure-only and safe
// during GUI-side recipe validation. Setting it true reads and hashes the
// source; callers must use a worker thread.
[[nodiscard]] bool validate_content_descriptor(
    const QVariantMap& descriptor,
    bool require_linked_source_available,
    QString* error = nullptr);

// Inspect filesystem state and verify the linked bytes against the descriptor.
// This reads and hashes the whole source, so callers must run it off the GUI
// thread. Structural recipe validation remains safe on the GUI thread.
// `status` is embedded, current, missing, changed, or unreadable.
[[nodiscard]] QVariantMap inspect_link_status(
    const QVariantMap& descriptor,
    QString* error = nullptr);

// Returns the exact source file bytes for embedded or linked descriptors.
// Linked files are re-hashed before returning, so stale or missing links fail;
// call from a worker thread for linked descriptors.
[[nodiscard]] QByteArray read_content_bytes(
    const QVariantMap& descriptor,
    QString* error = nullptr);

[[nodiscard]] QByteArray serialize_content_descriptor(
    const QVariantMap& descriptor,
    QString* error = nullptr);
[[nodiscard]] QVariantMap deserialize_content_descriptor(
    const QByteArray& bytes,
    QString* error = nullptr);

// Attach one reusable content object to a copy of a source layer. Linked
// descriptors trigger file reads and must be processed off the GUI thread. Only the
// copied layer ID and Smart Object source payload change; masks, styling,
// placement, transforms, visibility, and effects are carried through.
[[nodiscard]] QVariantMap create_instance(
    const QVariantMap& template_layer,
    const QVariantMap& content_descriptor,
    const QString& new_layer_id,
    QString* error = nullptr);

// Convert the selected existing source/pixel layers into instances of the
// same reusable object. Returns the original stack unchanged on any failure.
// Linked descriptors trigger file reads and must be processed off the GUI thread.
[[nodiscard]] QVariantList create_instances(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QVariantMap& content_descriptor,
    QString* error = nullptr);

// Replace selected content once for every instance in the same shared group.
// Each group's logical contentId is preserved, and all outer layer fields are
// left intact. The whole operation is atomic on validation/lock failures.
// Linked replacement descriptors trigger file reads and must be processed off
// the GUI thread.
[[nodiscard]] QVariantList replace_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QVariantMap& replacement_descriptor,
    QString* error = nullptr);

// Relink each selected shared group to a new source file. Every group update
// is atomic across all shared instances and preserves logical content IDs,
// instance IDs, and outer layer state. The caller must invoke this on a worker
// thread because it reads and hashes source bytes.
[[nodiscard]] QVariantList relink_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QString& new_source_path,
    QString* error = nullptr);
[[nodiscard]] QVariantList relink_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    const QString& new_source_path,
    int intrinsic_width,
    int intrinsic_height,
    QString* error = nullptr);

// Refresh each selected linked group from its current path, accepting an
// updated on-disk revision while retaining its shared identity and placements.
// All target groups are prepared before any returned layer is changed.
[[nodiscard]] QVariantList refresh_linked_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    QString* error = nullptr);
[[nodiscard]] QVariantList refresh_linked_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    int intrinsic_width,
    int intrinsic_height,
    QString* error = nullptr);

// Copy linked bytes into the recipe and convert every instance in each
// selected shared group to embedded content. Source files are read on the
// caller's worker thread; the resulting descriptor is portable.
[[nodiscard]] QVariantList embed_linked_contents(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    QString* error = nullptr);

// Give each selected instance its own logical contentId while retaining the
// same current source bytes and every outer layer field.
[[nodiscard]] QVariantList make_independent(
    const QVariantList& layer_stack,
    const QStringList& selected_layer_ids,
    QString* error = nullptr);

// Resolve an instance into the current layer-source payload shape. Linked
// instances expose sourcePath; embedded source files expose exact source bytes
// in pixelData with pixelDataEncoding="source-file" for worker-side decode.
// Embedded ordinary pixel layers expose the existing PNG pixelData shape.
// pixelWidth/Height remain the layer's placement bounds; intrinsic dimensions
// are separately reported as smartObjectIntrinsicWidth/Height so callers can
// fit the source without distorting it. Resolving linked content reads and
// hashes its file, so callers must use a worker thread. The input map is never
// modified.
[[nodiscard]] QVariantMap resolve_instance_source(
    const QVariantMap& layer,
    QString* error = nullptr);

}  // namespace photo_editor::desktop::smart_object
