#include "editor_preview_controller.h"
#include "photo_smart_document.h"
#include "photo_smart_object.h"
#include "photo_render_utils.h"
#include "photo_layer_source.h"
#include "photo_text_style.h"
#include "photo_point_text.h"
#include "photo_layer_style_light.h"
#include <QBuffer>
#include <QDir>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

#include <cmath>

namespace photo_editor::desktop {
namespace component = smart_object::nested_document;
namespace {
bool component_locked(const QVariantList& layers, int index) {
  QSet<QString> visited;
  for (int depth = 0; depth < 64 && index >= 0 && index < layers.size(); ++depth) {
    const auto layer = layers[index].toMap(); const auto id = layer.value("id").toString();
    if (layer.value("locked").toBool() || visited.contains(id)) return true;
    visited.insert(id); const auto parent = layer.value("parentId").toString(); if (parent.isEmpty()) return false;
    index = -1; for (int i = 0; i < layers.size(); ++i) if (layers[i].toMap().value("id").toString() == parent) { index = i; break; }
  }
  return true;
}
QString local_file(const QString& path) { const QUrl url(path); return url.isLocalFile() ? url.toLocalFile() : path; }
QVariantMap embedded_child(QVariantMap layer, const QString& replacement, QString* error) {
  auto source = layer; const auto prior = layer.value("smartObject").toMap();
  if (!replacement.isEmpty()) source.insert("sourcePath", local_file(replacement));
  else if (!prior.isEmpty()) {
    if (prior.value("policy").toString() == "embedded") return layer;
    source.insert("sourcePath", prior.value("sourcePath"));
  }
  source.insert("kind", "source");
  const QFileInfo input(source.value("sourcePath").toString());
  if (!input.isFile() || !input.isReadable() || input.size() <= 0 ||
      input.size() > 48LL * 1024 * 1024) {
    *error = "Component image input is unavailable, empty, or exceeds the 48 MiB embedded-content budget.";
    return {};
  }
  if (!smart_object::supports_nested_source_file_format(input.fileName())) {
    *error = "This source format cannot be embedded in a component. Use a supported raster, RAW, or HDR source.";
    return {};
  }
  auto descriptor = smart_object::create_content_descriptor(source, smart_object::ContentPolicy::Embedded, error);
  if (descriptor.isEmpty()) return {};
  if (!prior.isEmpty()) descriptor.insert("contentId", prior.value("contentId"));

  // Validate the exact bytes that the descriptor retains. The authoritative
  // source decoder applies EXIF/RAW orientation and retains scene-linear float
  // samples for OpenEXR and camera sources.
  const auto bytes = smart_object::read_content_bytes(descriptor, error);
  if (bytes.isEmpty()) return {};
  const auto suffix = QFileInfo(descriptor.value("fileName").toString()).suffix();
  QTemporaryFile decoded_source(QDir::tempPath() +
      QStringLiteral("/looksy-component-source-XXXXXX.") + suffix);
  if (!decoded_source.open() || decoded_source.write(bytes) != bytes.size() ||
      !decoded_source.flush()) {
    *error = "The original component image bytes could not be staged for source decoding.";
    return {};
  }
  const auto decoded_path = decoded_source.fileName();
  decoded_source.close();
  const auto decoded = decode_photo_layer_source_image(decoded_path);
  const auto pixel_count = decoded.isNull() ? 0 :
      static_cast<qint64>(decoded.width()) * static_cast<qint64>(decoded.height());
  if (decoded.isNull()) {
    *error = "The original component image bytes could not be decoded. No raster substitute was used.";
    return {};
  }
  if (decoded.width() <= 0 || decoded.height() <= 0 ||
      pixel_count > component::kMaximumCanvasPixels) {
    *error = "Component image dimensions are invalid or exceed 64 megapixels.";
    return {};
  }
  descriptor.insert("intrinsicWidth", decoded.width());
  descriptor.insert("intrinsicHeight", decoded.height());
  if (!smart_object::validate_content_descriptor(descriptor, false, error)) return {};

  const auto instance_id = layer.value("smartObjectInstanceId");
  auto next = smart_object::create_instance(layer, descriptor, QUuid::createUuid().toString(QUuid::WithoutBraces), error);
  if (!next.isEmpty()) next.insert("id", layer.value("id"));
  if (instance_id.isValid() && !next.isEmpty()) next.insert("smartObjectInstanceId", instance_id);
  return next;
}
}

bool EditorPreviewController::convertPhotoLayerToComponent(int index) { return preparePhotoComponentOperation(index, "convert"); }
bool EditorPreviewController::duplicatePhotoComponent(int index) { return preparePhotoComponentOperation(index, "duplicate"); }
bool EditorPreviewController::makePhotoComponentIndependent(int index) { return preparePhotoComponentOperation(index, "independent"); }
bool EditorPreviewController::preparePhotoComponentOperation(int index, const QString& operation) {
  const auto access = customer_edit_access_.enter(); const auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 || index >= layers.size() || component_locked(layers, index)) return false;
  const auto layer = layers[index].toMap(); if (layer.value("baseImageEdit").toBool()) return false;
  const auto id = layer.value("id").toString(), asset = asset_id_; const auto masks = mask_session_.stack(); const auto size = sourceImageSize();
  const auto document_light_angle = operation == "convert"
      ? documentPhotoLayerStyleLightAngle()
      : photo_layer_style_light::kDefaultAngleDegrees;
  professional_content_work_running_ = true; emit controlsChanged(); setStatus("Preparing editable component…");
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, layers, masks, asset, operation, document_light_angle] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false; emit controlsChanged();
    if (asset != asset_id_ || layers != layer_session_.layers() || masks != mask_session_.stack() ||
        (operation == "convert" && std::abs(documentPhotoLayerStyleLightAngle() - document_light_angle) >= 1e-9)) {
      setStatus("The document changed. Current edits were preserved."); return;
    }
    const auto error = result.value("error").toString(); if (!error.isEmpty()) { setStatus(error); return; }
    applyPhotoLayerStackChange(result.value("layers").toList(), "component_content", false,
        operation == "convert" ? "Create editable component" : operation == "duplicate" ? "Duplicate shared component" : "Make component independent");
  });
  watcher->setFuture(QtConcurrent::run([layers, masks, layer, id, index, size, operation, document_light_angle] {
    QString error; auto next = layers;
    if (operation == "convert") {
      // Embed only the chosen subtree, never mutate an unselected shared sibling.
      QSet<QString> selected{id}; bool changed = true;
      for (int depth = 0; changed && depth < 64; ++depth) {
        changed = false; for (const auto& item : layers) { const auto child = item.toMap(); const auto child_id = child.value("id").toString();
          if (!selected.contains(child_id) && selected.contains(child.value("parentId").toString())) { selected.insert(child_id); changed = true; }
        }
      }
      for (int i = 0; i < next.size(); ++i) { const auto child = next[i].toMap();
        if (selected.contains(child.value("id").toString()) && child.value("kind").toString() == "source") {
          const auto embedded = embedded_child(child, {}, &error); if (!error.isEmpty()) return QVariantMap{{"error", error}}; next[i] = embedded;
        }
      }
      next = component::create_component(next, masks, {id}, size.width(), size.height(), &error,
                                         document_light_angle);
    } else if (operation == "duplicate") {
      auto copy = component::duplicate_instance(layer, QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
      if (error.isEmpty()) { copy.insert("name", layer.value("name").toString() + " Copy"); next.insert(index, copy); }
    } else next = component::make_independent(layers, {id}, &error);
    return QVariantMap{{"layers", next}, {"error", error}};
  }));
  return true;
}

QVariantMap EditorPreviewController::smartDocumentEditState() const {
  if (professional_document_edit_.isEmpty() || professional_document_edit_.value("assetId").toString() != asset_id_) return {{"active", false}};
  const auto draft = professional_document_edit_.value("draft").toMap(); const auto children = draft.value("layers").toList(); QVariantList summaries;
  for (int i = 0; i < children.size(); ++i) {
    const auto child = children[i].toMap(); QVariantMap summary;
    for (const auto* key : {"id", "parentId", "name", "kind", "text", "fillColor", "textColor", "opacity", "enabled"}) if (child.contains(key)) summary.insert(key, child.value(key));
    summary.insert("locked", component_locked(children, i));
    if (child.contains("textTypography")) summary.insert("textColor", child.value("textTypography").toMap().value("color", child.value("textColor")));
    for (const auto* key : {"fillColor", "textColor"}) if (summary.contains(key)) { const QColor color(summary.value(key).toString()); if (color.isValid()) summary.insert(key, color.name(QColor::HexArgb)); }
    summaries.append(summary);
  }
  return {{"active", true}, {"sessionId", professional_document_edit_.value("sessionId")}, {"layerId", professional_document_edit_.value("layerId")},
      {"layers", summaries}, {"previewSource", professional_document_edit_.value("previewSource")}, {"previewBusy", professional_document_preview_running_},
      {"previewReady", !professional_document_preview_running_ && !professional_document_preview_pending_ && !professional_document_edit_.value("previewSource").toString().isEmpty()},
      {"error", professional_document_edit_.value("error")},
      {"applying", professional_document_edit_.value("applyAfterPreview").toBool()},
      {"width", draft.value("canvasWidth")}, {"height", draft.value("canvasHeight")}};
}
bool EditorPreviewController::beginPhotoComponentEditing(int index) {
  const auto access = customer_edit_access_.enter(); const auto layers = layer_session_.layers();
  if (!access || professional_content_work_running_ || index < 0 || index >= layers.size() || component_locked(layers, index)) return false;
  const auto layer = layers[index].toMap(); const auto document = layer.value("smartObjectDocument").toMap();
  if (document.isEmpty()) return false;
  cancelPhotoComponentEditing(); const auto asset = asset_id_; const auto masks = mask_session_.stack(); const auto generation = professional_document_generation_;
  professional_content_work_running_ = true; emit controlsChanged(); setStatus("Opening component contents…");
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, layers, masks, layer, document, asset, generation] {
    const auto error = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false;
    if (asset != asset_id_ || generation != professional_document_generation_ || layers != layer_session_.layers() || masks != mask_session_.stack()) { emit controlsChanged(); return; }
    if (!error.isEmpty()) { setStatus(error); emit controlsChanged(); return; }
    professional_document_edit_ = {{"assetId", asset}, {"layerId", layer.value("id")}, {"sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"baselineLayers", layers}, {"baselineMasks", masks}, {"draft", document}, {"expectedRevision", document.value("revision")}};
    emit controlsChanged(); requestPhotoComponentPreview();
  });
  watcher->setFuture(QtConcurrent::run([document] { QString error; if (!component::validate_document(document, &error) && error.isEmpty()) error = "Invalid component document."; return error; }));
  return true;
}
void EditorPreviewController::cancelPhotoComponentEditing() {
  ++professional_document_generation_; professional_document_edit_.clear(); professional_document_preview_pending_ = false; emit controlsChanged();
}
bool EditorPreviewController::changePhotoComponentChild(const QString& id, const QVariantMap& patch) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_ || professional_document_edit_.value("applyAfterPreview").toBool() || !smartDocumentEditState().value("active").toBool() || patch.isEmpty()) return false;
  auto draft = professional_document_edit_.value("draft").toMap(); auto children = draft.value("layers").toList();
  int index = -1; for (int i = 0; i < children.size(); ++i) if (children[i].toMap().value("id").toString() == id) { index = i; break; }
  if (index < 0 || component_locked(children, index)) return false;
  auto child = children[index].toMap(); const auto previous = child;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    const auto key = it.key(); const auto kind = child.value("kind").toString();
    if (key == "name") { if (it.value().toString().size() > 256) return false; child.insert(key, it.value().toString()); }
    else if (key == "enabled") { if (it.value().metaType() != QMetaType::fromType<bool>()) return false; child.insert(key, it.value()); }
    else if (key == "opacity") { const auto n = it.value().toDouble(); if (!std::isfinite(n) || n < 0 || n > 1) return false; child.insert(key, n); }
    else if (key == "text" && kind == "text") {
      const auto text = it.value().toString(); if (text.size() > 1048576) return false;
      if (child.contains("textTypography")) { auto typography = child.value("textTypography").toMap();
        const auto runs = typography.value("runs").toList();
        if (!runs.isEmpty()) { typography.insert("runs", remap_photo_text_runs(runs, child.value("text").toString(), text)); typography.insert("version", 10); }
        QString error; const auto style = decode_photo_text_style(QJsonObject::fromVariantMap(typography), &error);
        if (!style) { setStatus(error); return false; } child.insert("textTypography", encode_photo_text_style(*style).toVariantMap());
      }
      child.insert("text", text); if (child.contains("textTypography") && !fit_photo_point_text(&child)) return false;
    } else if ((key == "fillColor" && kind == "fill") || (key == "textColor" && kind == "text")) {
      const QColor color(it.value().toString()); if (!color.isValid()) return false; child.insert(key, color.name(QColor::HexArgb));
      if (key == "textColor" && child.contains("textTypography")) { auto typography = child.value("textTypography").toMap(); typography.insert("color", color.name(QColor::HexArgb)); child.insert("textTypography", typography); }
    } else return false;
  }
  if (child == previous) return false;
  children[index] = child; draft.insert("layers", children); professional_document_edit_.insert("draft", draft); professional_document_edit_.remove("error");
  emit controlsChanged(); requestPhotoComponentPreview(); return true;
}
bool EditorPreviewController::addPhotoComponentChild(const QString& kind) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_ || professional_document_edit_.value("applyAfterPreview").toBool() || !smartDocumentEditState().value("active").toBool() || (kind != "text" && kind != "fill")) return false;
  auto draft = professional_document_edit_.value("draft").toMap(); auto children = draft.value("layers").toList(); if (children.size() >= 4096) return false;
  QVariantMap child{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"kind", kind}, {"name", kind == "text" ? "Text" : "Fill"}, {"enabled", true}, {"opacity", 1.0},
      {"canvasWidth", draft.value("canvasWidth")}, {"canvasHeight", draft.value("canvasHeight")}};
  if (kind == "fill") child.insert("fillColor", "#4a78ff");
  else { child.insert("text", "Your text"); child.insert("textColor", "#ffffff"); child.insert("fontFamily", QGuiApplication::font().family()); child.insert("fontSize", 48);
    child.insert("pixelWidth", std::min(1024, draft.value("canvasWidth").toInt())); child.insert("pixelHeight", std::min(256, draft.value("canvasHeight").toInt())); }
  children.prepend(child); draft.insert("layers", children); professional_document_edit_.insert("draft", draft); emit controlsChanged(); requestPhotoComponentPreview(); return true;
}
bool EditorPreviewController::replacePhotoComponentImage(const QString& session_id, const QString& child_id, const QString& path) {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_ || professional_document_edit_.value("applyAfterPreview").toBool() || !smartDocumentEditState().value("active").toBool() || session_id != professional_document_edit_.value("sessionId").toString() || path.isEmpty()) return false;
  const auto draft = professional_document_edit_.value("draft").toMap(); const auto children = draft.value("layers").toList(); int index = -1;
  for (int i = 0; i < children.size(); ++i) if (children[i].toMap().value("id").toString() == child_id) { index = i; break; }
  if (index < 0 || component_locked(children, index) || children[index].toMap().value("kind").toString() != "source") return false;
  const auto generation = professional_document_generation_; const auto asset = asset_id_;
  professional_content_work_running_ = true; emit controlsChanged(); auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, generation, asset, draft] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false;
    if (asset != asset_id_ || generation != professional_document_generation_ || professional_document_edit_.value("draft").toMap() != draft) { emit controlsChanged(); return; }
    const auto error = result.value("error").toString(); if (!error.isEmpty()) { professional_document_edit_.insert("error", error); setStatus(error); emit controlsChanged(); return; }
    auto document = draft; document.insert("layers", result.value("layers").toList()); professional_document_edit_.insert("draft", document);
    professional_document_edit_.remove("error");
    emit controlsChanged(); requestPhotoComponentPreview();
  });
  watcher->setFuture(QtConcurrent::run([children, draft, index, path] {
    QString error; const auto child = children[index].toMap(); const auto embedded = embedded_child(child, path, &error);
    if (!error.isEmpty()) return QVariantMap{{"error", error}};
    auto next = children;
    if (!child.value("smartObject").toMap().isEmpty()) next = smart_object::replace_contents(children, {child.value("id").toString()}, embedded.value("smartObject").toMap(), &error);
    else next[index] = embedded;
    auto candidate = draft; candidate.insert("layers", next);
    if (error.isEmpty() && !component::validate_document(candidate, &error) && error.isEmpty()) error = "Unsupported component image content.";
    return QVariantMap{{"layers", next}, {"error", error}};
  }));
  return true;
}
bool EditorPreviewController::applyPhotoComponentEditing() {
  const auto access = customer_edit_access_.enter();
  if (!access || professional_content_work_running_ || !smartDocumentEditState().value("active").toBool()) return false;
  const auto edit = professional_document_edit_; const auto asset = asset_id_; const auto generation = professional_document_generation_;
  const auto baseline = edit.value("baselineLayers").toList();
  if (baseline != layer_session_.layers() || edit.value("baselineMasks").toList() != mask_session_.stack()) {
    const auto error = QStringLiteral("The parent document changed. Cancel and reopen the contents to keep those edits.");
    professional_document_edit_.insert("error", error); setStatus(error); emit controlsChanged(); return false;
  }
  for (const auto& item : baseline) { const auto layer = item.toMap();
    if (layer.value("id") == edit.value("layerId") && layer.value("smartObjectDocument").toMap() == edit.value("draft").toMap()) { cancelPhotoComponentEditing(); return true; }
  }
  if (professional_document_preview_running_ || professional_document_preview_pending_) {
    professional_document_edit_.insert("applyAfterPreview", true); emit controlsChanged(); return true;
  }
  if (professional_document_edit_.value("previewSource").toString().isEmpty()) return false;
  professional_document_edit_.remove("applyAfterPreview");
  professional_content_work_running_ = true; emit controlsChanged(); auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, asset, generation, baseline, edit] {
    const auto result = watcher->result(); watcher->deleteLater(); professional_content_work_running_ = false;
    if (asset != asset_id_ || generation != professional_document_generation_ || baseline != layer_session_.layers() || edit.value("baselineMasks").toList() != mask_session_.stack()) {
      const auto error = QStringLiteral("The document changed. Current edits were preserved.");
      if (asset == asset_id_ && generation == professional_document_generation_) professional_document_edit_.insert("error", error);
      emit controlsChanged(); setStatus(error); return;
    }
    const auto error = result.value("error").toString(); if (!error.isEmpty()) { professional_document_edit_.insert("error", error); setStatus(error); emit controlsChanged(); return; }
    const bool accepted = applyPhotoLayerStackChange(result.value("layers").toList(), "component_edit", false, "Edit shared component contents");
    if (accepted) cancelPhotoComponentEditing(); else emit controlsChanged();
  });
  watcher->setFuture(QtConcurrent::run([edit, baseline] { QString error; const auto next = component::apply_shared_document(baseline, {edit.value("layerId").toString()},
      edit.value("draft").toMap(), edit.value("expectedRevision").toInt(), &error); return QVariantMap{{"layers", next}, {"error", error}}; }));
  return true;
}
void EditorPreviewController::requestPhotoComponentPreview() {
  if (!smartDocumentEditState().value("active").toBool()) return;
  if (professional_document_preview_running_) { professional_document_preview_pending_ = true; return; }
  const auto draft = professional_document_edit_.value("draft").toMap(); const auto generation = professional_document_generation_;
  professional_document_preview_running_ = true; professional_document_preview_pending_ = false; emit controlsChanged();
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, draft, generation] {
    const auto source = watcher->result(); watcher->deleteLater(); professional_document_preview_running_ = false;
    const auto current = generation == professional_document_generation_ && professional_document_edit_.value("draft").toMap() == draft;
    if (current) {
      professional_document_edit_.insert("previewSource", source);
      if (source.isEmpty()) {
        professional_document_edit_.remove("applyAfterPreview");
        professional_document_edit_.insert("error", QStringLiteral("The component preview could not be rendered."));
      }
    }
    emit controlsChanged();
    if (professional_document_preview_pending_) { professional_document_preview_pending_ = false; requestPhotoComponentPreview(); }
    else if (current && !source.isEmpty() && professional_document_edit_.value("applyAfterPreview").toBool()) applyPhotoComponentEditing();
  });
  const auto working_profile = currentState().working_color_profile;
  const auto rendering_model = currentState().photographic_rendering_model;
  watcher->setFuture(QtConcurrent::run([draft, working_profile, rendering_model] {
    EditRecipeState state; state.working_color_profile = working_profile; state.photographic_rendering_model = rendering_model; state.layers = draft.value("layers").toList(); state.mask_stack = draft.value("maskStack").toList();
    state.document_layer_style_light_angle = draft.value(
        "documentLayerStyleLightAngle", photo_layer_style_light::kDefaultAngleDegrees)
        .toDouble();
    const QSize canvas(draft.value("canvasWidth").toInt(), draft.value("canvasHeight").toInt());
    const auto size = canvas.scaled(512, 512, Qt::KeepAspectRatio); QImage base(size, QImage::Format_ARGB32_Premultiplied); base.fill(Qt::transparent); base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    RenderMaskOptions masks; masks.source_geometry = {canvas, QRect(QPoint(), canvas)};
    const auto image = render_output_image_with_backend(base, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, nullptr, nullptr, masks);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); if (image.isNull() || !image.save(&buffer, "PNG")) return QString{};
    return QString("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
  }));
}
}  // namespace photo_editor::desktop
