#include "photo_component_workspace_controller.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "edit_adjustment_patch.h"
#include "photo_smart_document.h"
#include "photo_smart_object.h"
#include "photo_render_utils.h"
#include "photo_layer_source.h"
#include "photo_layer_style_light.h"
#include "photo_text_dimension.h"

#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

#include <cmath>

namespace photo_editor::desktop {
namespace component = smart_object::nested_document;
namespace {
class ScopedImageSink final : public EditorPreviewImageSink {
 public:
  ScopedImageSink(EditorPreviewImageSink* target, QString prefix)
      : target_(target), prefix_(std::move(prefix) + QLatin1Char('/')) {}
  ~ScopedImageSink() override {
    if (target_) for (const auto& key : keys_) target_->clearImage(prefix_ + key);
  }
  void setImage(const QString& key, const QImage& image) override {
    keys_.insert(key);
    if (target_) target_->setImage(prefix_ + key, image);
    else images_.insert(key, image);
  }
  void clearImage(const QString& key) override {
    if (target_) target_->clearImage(prefix_ + key);
    images_.remove(key);
  }
  QImage image(const QString& key) const override {
    return target_ ? target_->image(prefix_ + key) : images_.value(key);
  }
 private:
  EditorPreviewImageSink* target_;
  QString prefix_;
  QSet<QString> keys_;
  QHash<QString, QImage> images_;
};

bool editor_work_active(const EditorPreviewController* editor) {
  return !editor || editor->processing() || editor->fullRenderRunning() ||
      editor->professionalToolsBusy() || editor->photoPaintBusy() ||
      editor->brushStrokeActive() || editor->selectionWorkBusy() ||
      editor->selectionEditSessionActive() || editor->photoExportBusy();
}

EditRecipeState global_settings(EditRecipeState state) {
  // Selections, masks and local layers belong to the child document. The
  // legacy mask controls mirror the selected mask and are not global grading.
  const EditRecipeState defaults;
  state.layers.clear(); state.mask_stack.clear(); state.selection_state.clear();
  state.document_layer_style_light_angle = defaults.document_layer_style_light_angle;
  state.active_mask_index = -1; state.active_history_branch = defaults.active_history_branch;
  state.recipe_snapshots.clear();
  state.linear_mask_enabled = defaults.linear_mask_enabled;
  state.linear_mask_exposure = defaults.linear_mask_exposure;
  state.linear_mask_temperature = defaults.linear_mask_temperature;
  state.linear_mask_feather = defaults.linear_mask_feather;
  state.linear_mask_position = defaults.linear_mask_position;
  state.linear_mask_rotation = defaults.linear_mask_rotation;
  state.radial_mask_enabled = defaults.radial_mask_enabled;
  state.radial_mask_exposure = defaults.radial_mask_exposure;
  state.radial_mask_temperature = defaults.radial_mask_temperature;
  state.radial_mask_feather = defaults.radial_mask_feather;
  state.radial_mask_center_x = defaults.radial_mask_center_x;
  state.radial_mask_center_y = defaults.radial_mask_center_y;
  state.radial_mask_radius_x = defaults.radial_mask_radius_x;
  state.radial_mask_radius_y = defaults.radial_mask_radius_y;
  state.radial_mask_rotation = defaults.radial_mask_rotation;
  return state;
}

bool preflight_embedded_source(QVariantMap* descriptor, QString* error) {
  if (descriptor == nullptr) {
    if (error) *error = QStringLiteral("No embedded component source was provided.");
    return false;
  }
  const auto file_name = descriptor->value(QStringLiteral("fileName")).toString();
  if (!smart_object::supports_nested_source_file_format(file_name)) {
    if (error) {
      *error = QStringLiteral(
          "This source format is not supported inside a component. Keep it linked in the parent or use a supported raster, RAW, or HDR source.");
    }
    return false;
  }

  auto bytes = smart_object::read_content_bytes(*descriptor, error);
  if (bytes.isEmpty()) return false;
  if (bytes.size() > 48LL * 1024LL * 1024LL) {
    if (error) {
      *error = QStringLiteral(
          "A component image exceeds the 48 MiB input budget.");
    }
    return false;
  }

  const auto suffix = QFileInfo(file_name).suffix();
  QTemporaryFile source(QDir::tempPath() +
      QStringLiteral("/looksy-component-source-XXXXXX.") + suffix);
  if (!source.open() || source.write(bytes) != bytes.size() || !source.flush()) {
    if (error) {
      *error = QStringLiteral(
          "The original source bytes could not be staged for component decoding.");
    }
    return false;
  }
  const auto source_path = source.fileName();
  source.close();

  // Decode the exact bytes retained by the Smart Object descriptor. This uses
  // the ordinary source path, including RAW characterization and float HDR
  // samples, without materializing a lossy replacement image.
  const auto decoded = decode_photo_layer_source_image(source_path);
  if (decoded.isNull()) {
    if (error) {
      *error = QStringLiteral(
          "The original source bytes could not be decoded for component Apply. No raster substitute was used.");
    }
    return false;
  }
  const auto pixel_count = static_cast<qint64>(decoded.width()) *
      static_cast<qint64>(decoded.height());
  if (decoded.width() <= 0 || decoded.height() <= 0 ||
      pixel_count > component::kMaximumCanvasPixels) {
    if (error) {
      *error = QStringLiteral(
          "A component image has unsupported decoded dimensions or exceeds 64 megapixels.");
    }
    return false;
  }

  descriptor->insert(QStringLiteral("intrinsicWidth"), decoded.width());
  descriptor->insert(QStringLiteral("intrinsicHeight"), decoded.height());
  return smart_object::validate_content_descriptor(*descriptor, false, error);
}

bool embed_workspace_sources(QVariantList* layers, QString* error, int depth = 0) {
  if (depth > component::kMaximumLayerDepth) { *error = "Component layer nesting is too deep."; return false; }
  // Reuse each embedded source descriptor and its byte-backed decode check
  // across all instances that share the same logical content ID.
  QHash<QString, QVariantMap> descriptors;
  for (auto& value : *layers) {
    auto layer = value.toMap();
    const auto prior = layer.value("smartObject").toMap();
    if (prior.value("policy").toString() == "linked") {
      *error = "Embed linked image content before applying this component. Linked files can remain in the parent document.";
      return false;
    }
    if (layer.value("kind").toString() == "source") {
      const auto prior_id = prior.value("contentId").toString();
      auto descriptor = descriptors.value(prior_id);
      if (descriptor.isEmpty()) {
        const auto already_embedded =
            prior.value("policy").toString() == "embedded";
        if (already_embedded) {
          descriptor = prior;
        } else {
          auto source = layer;
          if (!prior.isEmpty()) {
            source.insert("sourcePath", prior.value("sourcePath"));
          }
          const QUrl url(source.value("sourcePath").toString());
          if (url.isLocalFile()) source.insert("sourcePath", url.toLocalFile());
          const QFileInfo file(source.value("sourcePath").toString());
          if (!file.isFile() || !file.isReadable()) {
            *error = "A component image is unavailable or unreadable.";
            return false;
          }
          if (file.size() <= 0 || file.size() > 48LL * 1024 * 1024) {
            *error = "A component image is empty or exceeds the 48 MiB input budget.";
            return false;
          }
          if (!smart_object::supports_nested_source_file_format(file.fileName())) {
            *error = "This source format is not supported inside a component. Keep it linked in the parent or use a supported raster, RAW, or HDR source.";
            return false;
          }
          source.insert("sourcePath", file.absoluteFilePath());
          descriptor = smart_object::create_content_descriptor(
              source, smart_object::ContentPolicy::Embedded, error);
          if (descriptor.isEmpty()) return false;
          if (!prior_id.isEmpty()) descriptor.insert("contentId", prior_id);
        }
        if (!preflight_embedded_source(&descriptor, error)) return false;
        const auto cache_id = prior_id.isEmpty()
            ? descriptor.value("contentId").toString() : prior_id;
        descriptors.insert(cache_id, descriptor);
      }
      if (prior.value("policy").toString() != "embedded") {
        const auto instance = layer.value("smartObjectInstanceId");
        auto embedded = smart_object::create_instance(layer, descriptor,
            QUuid::createUuid().toString(QUuid::WithoutBraces), error);
        if (embedded.isEmpty()) return false;
        embedded.insert("id", layer.value("id"));
        if (instance.isValid()) embedded.insert("smartObjectInstanceId", instance);
        layer = embedded;
      } else {
        layer.insert("smartObject", descriptor);
        layer.insert("sourcePath", QString());
      }
      layer.insert("smartObjectIntrinsicWidth",
                   descriptor.value("intrinsicWidth"));
      layer.insert("smartObjectIntrinsicHeight",
                   descriptor.value("intrinsicHeight"));
    }
    for (const auto* key : {"children", "layers"}) if (layer.contains(key)) {
      auto children = layer.value(key).toList();
      if (!embed_workspace_sources(&children, error, depth + 1)) return false;
      layer.insert(key, children);
    }
    value = layer;
  }
  return true;
}
}  // namespace

struct PhotoComponentWorkspaceController::Frame {
  QPointer<EditorPreviewController> parent;
  std::unique_ptr<ScopedImageSink> sink;
  std::unique_ptr<EditorPreviewController> editor;
  std::unique_ptr<QTemporaryDir> storage;
  QString name, layer_id, parent_asset, session_id, child_asset;
  quint64 parent_generation = 0;
  QVariantMap document, prepared;
  EditRecipeState initial_state;
  bool preparing_source = false;
  bool publishing = false;
  bool waiting_for_render = false;
  ~Frame() { editor.reset(); sink.reset(); storage.reset(); }
};

PhotoComponentWorkspaceController::PhotoComponentWorkspaceController(
    EditorPreviewController* editor, QObject* parent) : QObject(parent), root_(editor) {
  observe(editor);
}
PhotoComponentWorkspaceController::~PhotoComponentWorkspaceController() {
  ++generation_;
  if (opening_ && opening_->parent && opening_->parent->professional_document_generation_ == opening_->parent_generation)
    opening_->parent->cancelPhotoComponentEditing();
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
    if ((*it)->parent && (*it)->parent->professional_document_edit_.value("sessionId").toString() == (*it)->session_id)
      (*it)->parent->cancelPhotoComponentEditing();
  frames_.clear();
}
bool PhotoComponentWorkspaceController::active() const { return opening_ || !frames_.empty(); }
bool PhotoComponentWorkspaceController::busy() const { return opening_ || applying_; }
EditorPreviewController* PhotoComponentWorkspaceController::controller() const {
  return frames_.empty() ? root_.data() : frames_.back()->editor.get();
}
QObject* PhotoComponentWorkspaceController::qmlController() const { return controller(); }
QVariantList PhotoComponentWorkspaceController::breadcrumbs() const {
  QVariantList result{{QVariantMap{{"name", tr("Document")}, {"depth", 0}}}};
  for (std::size_t i = 0; i < frames_.size(); ++i)
    result.append(QVariantMap{{"name", frames_[i]->name}, {"depth", static_cast<int>(i + 1)}});
  if (opening_) result.append(QVariantMap{{"name", opening_->name}, {"depth", static_cast<int>(frames_.size() + 1)}});
  return result;
}
bool PhotoComponentWorkspaceController::canApply() const {
  const auto* child = controller();
  return active() && !busy() && child && !editor_work_active(child) &&
      child->previewFailureDetail().isEmpty() && child->previewFailureStage().isEmpty() &&
      !child->history_service_.transactionActive();
}
void PhotoComponentWorkspaceController::fail(const QString& error) { error_ = error; emit stateChanged(); }
void PhotoComponentWorkspaceController::observe(EditorPreviewController* editor) {
  if (!editor) return;
  const auto changed = [this] {
    if (opening_) continueOpening();
    if (applying_) finishApply();
    emit stateChanged();
  };
  connect(editor, &EditorPreviewController::controlsChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::processingChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::historyChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::photoPaintStateChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::brushStrokeActiveChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::maskStateChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::previewChanged, this, changed, Qt::QueuedConnection);
  connect(editor, &EditorPreviewController::assetChanged, this, changed, Qt::QueuedConnection);
}
bool PhotoComponentWorkspaceController::open(int layer_index) {
  auto* parent = controller();
  if (busy() || editor_work_active(parent) || parent->history_service_.transactionActive()) {
    fail(tr("Finish the current edit before opening component contents.")); return false;
  }
  if (frames_.size() >= static_cast<std::size_t>(component::kMaximumDocumentDepth)) {
    fail(tr("The component nesting limit has been reached.")); return false;
  }
  if (!parent->professional_document_edit_.isEmpty()) {
    fail(tr("Finish or cancel the current component draft first.")); return false;
  }
  const auto layers = parent->layer_session_.layers();
  if (layer_index < 0 || layer_index >= layers.size()) return false;
  const auto layer = layers[layer_index].toMap();
  auto frame = std::make_unique<Frame>();
  frame->parent = parent; frame->name = layer.value("name", tr("Component")).toString();
  frame->layer_id = layer.value("id").toString(); frame->parent_asset = parent->assetId();
  if (!parent->beginPhotoComponentEditing(layer_index)) {
    fail(tr("This component cannot be opened. Check its content and locked parent layers.")); return false;
  }
  frame->parent_generation = parent->professional_document_generation_;
  opening_ = std::move(frame); error_.clear(); ++generation_; emit stateChanged();
  QTimer::singleShot(0, this, [this] { continueOpening(); });
  return true;
}
void PhotoComponentWorkspaceController::continueOpening() {
  if (!opening_ || opening_->preparing_source) return;
  auto* parent = opening_->parent.data();
  if (!parent || parent->assetId() != opening_->parent_asset ||
      parent->professional_document_generation_ != opening_->parent_generation) {
    opening_.reset(); fail(tr("The parent changed while the component was opening.")); return;
  }
  if (parent->professionalToolsBusy()) return;
  const auto edit = parent->professional_document_edit_;
  if (edit.isEmpty() || edit.value("layerId").toString() != opening_->layer_id) {
    const auto error = parent->status(); opening_.reset(); fail(error); return;
  }
  opening_->document = edit.value("draft").toMap();
  opening_->session_id = edit.value("sessionId").toString();
  opening_->storage = std::make_unique<QTemporaryDir>();
  if (!opening_->storage->isValid()) {
    parent->cancelPhotoComponentEditing(); opening_.reset(); fail(tr("Cannot create the temporary component canvas.")); return;
  }
  opening_->preparing_source = true;
  const auto generation = generation_;
  const auto path = opening_->storage->filePath(QStringLiteral("transparent-source.png"));
  const QSize size(opening_->document.value("canvasWidth").toInt(), opening_->document.value("canvasHeight").toInt());
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, generation] {
    const auto error = watcher->result(); watcher->deleteLater(); finishOpening(generation, error);
  });
  watcher->setFuture(QtConcurrent::run([path, size] {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent); image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return !image.isNull() && image.save(path, "PNG") ? QString{} : QStringLiteral("Could not prepare the transparent component source.");
  }));
}
void PhotoComponentWorkspaceController::finishOpening(quint64 generation, const QString& error) {
  if (generation != generation_ || !opening_) return;
  auto* parent = opening_->parent.data();
  if (!error.isEmpty() || !parent || parent->assetId() != opening_->parent_asset ||
      parent->professional_document_edit_.value("sessionId").toString() != opening_->session_id) {
    if (parent && parent->professional_document_generation_ == opening_->parent_generation) parent->cancelPhotoComponentEditing();
    opening_.reset(); fail(error.isEmpty() ? tr("The parent changed while the component was opening.") : error); return;
  }
  auto child = std::make_unique<EditorPreviewController>();
  child->transient_document_session_ = true;
  child->setCatalogRecipePersistenceEnabled(false);
  child->mask_session_.setSelectionStorageDirectory({});
  const QPointer<EditorPreviewController> owner(parent);
  child->setEditAccessPolicy([owner] {
    if (!owner) return false;
    const auto access = owner->customer_edit_access_.enter(); return static_cast<bool>(access);
  });
  const auto identity = QUuid::createUuid().toString(QUuid::WithoutBraces);
  child->preview_image_namespace_ = QStringLiteral("component-") + identity;
  opening_->sink = std::make_unique<ScopedImageSink>(root_ ? root_->image_provider_ : nullptr, child->preview_image_namespace_);
  child->setImageProvider(opening_->sink.get());
  const auto parent_state = parent->currentState();
  EditRecipeState state;
  state.working_color_profile = parent_state.working_color_profile;
  state.photographic_rendering_model = parent_state.photographic_rendering_model;
  state.preview_color_space = parent_state.preview_color_space;
  state.preview_output_mode = parent_state.preview_output_mode;
  state.output_color_space = parent_state.output_color_space;
  state.output_headroom = parent_state.output_headroom;
  state.layers = opening_->document.value("layers").toList();
  state.mask_stack = opening_->document.value("maskStack").toList();
  state.document_layer_style_light_angle = opening_->document.value(
      "documentLayerStyleLightAngle", photo_layer_style_light::kDefaultAngleDegrees)
      .toDouble();
  opening_->child_asset = QStringLiteral("component-workspace-") + identity;
  child->loadAsset(opening_->child_asset, opening_->storage->filePath(QStringLiteral("transparent-source.png")),
      {}, {}, {}, {}, serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound), true);
  opening_->initial_state = child->currentState();
  opening_->editor = std::move(child);
  observe(opening_->editor.get());
  frames_.push_back(std::move(opening_)); emit stateChanged();
}
bool PhotoComponentWorkspaceController::apply() {
  if (active() && !busy() && !frames_.empty()) {
    auto* child = controller();
    if ((child->processing() || child->fullRenderRunning()) && !child->professionalToolsBusy() &&
        !child->photoPaintBusy() && !child->brushStrokeActive() && !child->selectionWorkBusy() &&
        !child->selectionEditSessionActive() && !child->history_service_.transactionActive() && !child->photoExportBusy()) {
      frames_.back()->waiting_for_render = true; applying_ = true; error_.clear(); emit stateChanged(); return true;
    }
  }
  if (!canApply()) { fail(tr("Wait for rendering and finish the current stroke, selection or text edit before applying.")); return false; }
  auto& frame = *frames_.back(); auto* child = frame.editor.get(); auto* parent = frame.parent.data();
  if (!parent || parent->assetId() != frame.parent_asset || child->assetId() != frame.child_asset ||
      parent->professional_document_edit_.value("sessionId").toString() != frame.session_id) {
    fail(tr("The parent or component session changed. The draft was retained; cancel and reopen to continue.")); return false;
  }
  const auto state = child->currentState();
  if (!(global_settings(state) == global_settings(frame.initial_state))) {
    fail(tr("Component contents use layer adjustments. Undo global grading, crop or output-setting changes before applying.")); return false;
  }
  if (child->history_service_.transactionActive()) { fail(tr("Finish the current edit before applying.")); return false; }
  const bool document_content_unchanged =
      component::strip_transient_layer_view_fields(state.layers) ==
          component::strip_transient_layer_view_fields(frame.initial_state.layers) &&
      normalize_mask_stack(state.mask_stack) == normalize_mask_stack(frame.initial_state.mask_stack) &&
      std::abs(state.document_layer_style_light_angle -
               frame.initial_state.document_layer_style_light_angle) < 1e-9;
  if (document_content_unchanged) {
    frame.prepared = frame.document; frame.publishing = true; applying_ = true; error_.clear(); emit stateChanged();
    if (!parent->applyPhotoComponentEditing()) {
      frame.publishing = false; applying_ = false;
      fail(parent->professional_document_edit_.value("error").toString().isEmpty()
          ? tr("The parent changed. Cancel and reopen the component.") : parent->professional_document_edit_.value("error").toString());
      return false;
    }
    finishApply(); return true;
  }
  auto draft = frame.document;
  draft.insert("layers", component::strip_transient_layer_view_fields(state.layers));
  draft.insert("maskStack", normalize_mask_stack(state.mask_stack));
  if (draft.contains("documentLayerStyleLightAngle") ||
      photo_layer_style_light::requires_document_light_capability(state)) {
    draft.insert("version", component::kDocumentLayerStyleLightVersion);
    draft.insert("documentLayerStyleLightAngle",
                 state.document_layer_style_light_angle);
  } else {
    draft.remove("documentLayerStyleLightAngle");
    draft.insert("version", component::kVersion);
  }
  if (photo_text_dimensions_present_in_layers(state.layers))
    draft.insert("version", component::kTextDimensionVersion);
  applying_ = true; frame.publishing = false; error_.clear();
  const auto generation = generation_; const auto session_id = frame.session_id;
  emit stateChanged();
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, generation, session_id, state] {
    const auto result = watcher->result(); watcher->deleteLater();
    if (generation != generation_ || frames_.empty() || frames_.back()->session_id != session_id) return;
    auto& current = *frames_.back(); auto* target = current.parent.data();
    auto captured = state; auto now = current.editor->currentState();
    captured.layers = component::strip_transient_layer_view_fields(captured.layers);
    now.layers = component::strip_transient_layer_view_fields(now.layers);
    const auto failure = result.value("error").toString();
    if (!failure.isEmpty() || !(captured == now) || !target || target->assetId() != current.parent_asset ||
        target->professional_document_edit_.value("sessionId").toString() != session_id) {
      applying_ = false; fail(failure.isEmpty() ? tr("The document changed while Apply was preparing. The draft was retained.") : failure); return;
    }
    current.prepared = result.value("document").toMap(); current.publishing = true;
    target->professional_document_edit_.insert("draft", current.prepared);
    target->professional_document_edit_.remove("error");
    target->requestPhotoComponentPreview();
    if (!target->applyPhotoComponentEditing()) {
      applying_ = false; current.publishing = false;
      fail(target->professional_document_edit_.value("error").toString().isEmpty()
          ? tr("The parent rejected Apply. The component draft is still open.") : target->professional_document_edit_.value("error").toString());
    }
    finishApply();
  });
  watcher->setFuture(QtConcurrent::run([draft]() mutable {
    QString error; auto layers = draft.value("layers").toList();
    if (embed_workspace_sources(&layers, &error)) {
      draft.insert("layers", layers);
      if (!component::validate_document(draft, &error) && error.isEmpty()) error = QStringLiteral("Invalid component document.");
    }
    return QVariantMap{{"error", error}, {"document", draft}};
  }));
  return true;
}
void PhotoComponentWorkspaceController::finishApply() {
  if (!applying_ || frames_.empty()) return;
  if (frames_.back()->waiting_for_render) {
    auto* child = controller();
    if (editor_work_active(child)) return;
    frames_.back()->waiting_for_render = false; applying_ = false;
    if (!child->previewFailureDetail().isEmpty() || !child->previewFailureStage().isEmpty()) {
      fail(tr("The component preview failed. Fix the edit or cancel before applying.")); return;
    }
    apply(); return;
  }
  if (!frames_.back()->publishing) return;
  auto& frame = *frames_.back(); auto* parent = frame.parent.data();
  if (!parent || parent->assetId() != frame.parent_asset) {
    applying_ = false; fail(tr("The parent changed. The component draft was retained.")); return;
  }
  if (parent->professionalToolsBusy() || parent->professional_document_preview_running_) return;
  if (!parent->professional_document_edit_.isEmpty()) {
    const auto error = parent->professional_document_edit_.value("error").toString();
    if (!error.isEmpty()) { applying_ = false; frame.publishing = false; fail(error); }
    return;
  }
  QVariantMap published;
  for (const auto& value : parent->layer_session_.layers()) {
    const auto layer = value.toMap();
    if (layer.value("id").toString() == frame.layer_id) published = layer.value("smartObjectDocument").toMap();
  }
  auto expected = frame.prepared; expected.insert("revision", published.value("revision"));
  if (published.isEmpty() || expected != published) {
    applying_ = false; fail(tr("The parent did not accept this component revision. The draft was retained.")); return;
  }
  // Retarget QML while its outgoing controller is still alive. Destroying it
  // first clears QObject wrappers before bindings can restore the parent.
  std::shared_ptr<Frame> finished_frame(std::move(frames_.back()));
  frames_.pop_back(); applying_ = false; error_.clear(); ++generation_; emit stateChanged();
  QTimer::singleShot(0, this, [finished_frame = std::move(finished_frame)]() mutable { finished_frame.reset(); });
}
bool PhotoComponentWorkspaceController::cancel() {
  if (!active()) return false;
  ++generation_; applying_ = false;
  std::shared_ptr<Frame> cancelled_frame;
  if (opening_) {
    if (opening_->parent && opening_->parent->professional_document_generation_ == opening_->parent_generation)
      opening_->parent->cancelPhotoComponentEditing();
    opening_.reset();
  } else {
    auto& frame = *frames_.back();
    if (frame.parent && frame.parent->professional_document_edit_.value("sessionId").toString() == frame.session_id)
      frame.parent->cancelPhotoComponentEditing();
    frame.editor->cancelAllPhotoPaint(); frame.editor->cancelPendingAssetScopedWork();
    cancelled_frame = std::move(frames_.back());
    frames_.pop_back();
  }
  error_.clear(); emit stateChanged();
  if (cancelled_frame)
    QTimer::singleShot(0, this, [cancelled_frame = std::move(cancelled_frame)]() mutable { cancelled_frame.reset(); });
  return true;
}
}  // namespace photo_editor::desktop
